#include "platform/common/method_handle_lookup.h"
#include <atomic>
#include <android/api-level.h>
#include <cstring>
#include <mutex>
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/android11/method_lookup.h"
#include "platform/abi/arm64/android15/method_lookup.h"
#include "platform/symbols/method_handle_lookup_symbols.h"

namespace artdcevm::method_handle_lookup {
namespace {
using Lookup = jobject (*)(JNIEnv*, jobject, jstring, jobjectArray);
Lookup original = nullptr;
void* target = nullptr;
void* hook = nullptr;
jvmtiEnv* environment = nullptr;
jclass method_type = nullptr, access_error = nullptr, internal_error = nullptr, missing_error = nullptr;
jfieldID return_field = nullptr, class_flags_field = nullptr;
jmethodID instance_method = nullptr, find_virtual = nullptr;
jmethodID public_recursive = nullptr, interface_recursive = nullptr;
jmethodID reflected_return = nullptr, reflected_parameters = nullptr;
jmethodID find_static = nullptr, declared_method = nullptr, class_get_method = nullptr;
jint static_type_slot = -1;
JavaVM* runtime_vm = nullptr;
// base/pointer_size.h, fixed 11--17: the helper runs under the FastNative
// caller's shared mutator access and does not allocate or suspend.
void* find_interface = nullptr;
void* find_class = nullptr;
bool inherited_static_verified = false;
bool nonpublic_static_verified = false;
thread_local bool probing_nonpublic_static = false;
thread_local unsigned observed_nonpublic_static = 0;
thread_local bool probing_inherited_static = false, observed_inherited_static = false;
int runtime_api = 0;
void* (*decode_global)(JavaVM*, jobject) = nullptr;
jobject (*new_local)(JNIEnv*, void*) = nullptr;
bool static_verified = false;
thread_local bool observed_static = false, observed_static_class = false;
jint type_slot = -1;
std::atomic<bool> enabled{false};
bool probe_verified = false;
thread_local bool probing = false, observed = false, preparing = false;
thread_local bool observed_recursive = false;
std::mutex install_mutex;

template <typename T> struct Local {
  JNIEnv* jni;
  T value{};
  ~Local() { if (value) jni->DeleteLocalRef(value); }
};
struct Text {
  char* value = nullptr;
  ~Text() { if (value) environment->Deallocate(reinterpret_cast<unsigned char*>(value)); }
};
size_t ReturnOffset(std::string_view signature) {
  if (signature.empty() || signature[0] != '(') return std::string_view::npos;
  size_t at = 1;
  while (at < signature.size() && signature[at] != ')') {
    if (signature[at] == 'L') {
      at = signature.find(';', at);
      if (at == std::string_view::npos) return at;
    }
    ++at;
  }
  return at < signature.size() ? at + 1 : std::string_view::npos;
}
bool ServiceFailure(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(internal_error, "live MethodHandle lookup argument unavailable");
  return true;
}
// Fixed Class.java (android-13.0.0_r1 / android-16.0.0_r1): traversal alternates
// getPublicMethodRecursive/findInterfaceMethod before getInstanceMethod and
// Lookup.findVirtual. Match the complete chain, never an arbitrary ancestor
// named findVirtual. Read bounded chunks so hierarchy depth is not capped.
// -1 means another caller; -2 means a failed public stack service.
jint RecursiveVirtualDepth(jvmtiFrameInfo (&frames)[4], jint count) {
  jint start = 0;
  bool failed = false;
  const auto method_at = [&](jint depth) -> jmethodID {
    if (depth >= start + count) {
      start = depth;
      if (environment->GetStackTrace(nullptr, start, 4, frames, &count)) {
        failed = true; return nullptr;
      }
    }
    return depth < start + count ? frames[depth - start].method : nullptr;
  };
  jint depth = 1;
  while (public_recursive && interface_recursive && method_at(depth) == public_recursive &&
         method_at(depth + 1) == interface_recursive) {
    depth += 2;
    if (method_at(depth) == instance_method) {
      if (method_at(depth + 1) == find_virtual) return depth + 1;
      break;
    }
  }
  return failed ? -2 : -1;
}
// ART still chooses the declaration through getInstanceMethod. At each native
// declared-method step, public JVMTI describes complete signatures before
// libcore's parameter/return Class identity checks. A descriptor match with a
// foreign-loader Class is an access/linkage failure on JBR, not a missing member.
// Interface static hiding retains its earlier invocation-kind precedence.
bool ResolveLookup(JNIEnv* jni, jobject klass, jstring name, jobjectArray args, jobject* result) {
  if (!name || !args) return false;  // Preserve the original null checks.
  const jint flags = jni->GetIntField(klass, class_flags_field);
  // dex/modifiers.h, fixed 11--17: leave obsolete mirrors to the original check.
  constexpr jint kAccObsoleteObject = 0x00200000;
  if (flags & kAccObsoleteObject) return false;
  jvmtiFrameInfo frames[4]{};
  jint count = 0;
  if (environment->GetStackTrace(nullptr, 0, 4, frames, &count)) return ServiceFailure(jni);
  jint virtual_depth = count >= 3 && frames[1].method == instance_method && frames[2].method == find_virtual ? 2 : -1;
  const bool static_lookup = (probing || static_verified) && runtime_vm && find_interface && decode_global && new_local &&
      static_type_slot >= 0 && count == 4 && frames[1].method == class_get_method &&
      frames[2].method == declared_method && frames[3].method == find_static;
  if (virtual_depth < 0 && !static_lookup && count >= 2 && frames[1].method == public_recursive)
    virtual_depth = RecursiveVirtualDepth(frames, count);
  if (virtual_depth == -2) return ServiceFailure(jni);
  const bool virtual_lookup = virtual_depth >= 0;
  const bool recursive_lookup = virtual_depth > 2;
  if (!virtual_lookup && !static_lookup) return false;
  if (static_lookup && !(flags & evolution::kAccPublic) &&
      ((flags & evolution::kAccInterface) || !(probing_nonpublic_static || nonpublic_static_verified))) return false;
  Local<jobject> loader{jni};
  if (environment->GetClassLoader(static_cast<jclass>(klass), &loader.value)) return ServiceFailure(jni);
  if (!loader.value) return false; // Boot interfaces cannot be enhanced; keep hidden-API filtering.
  struct Utf {
    JNIEnv* jni; jstring string; const char* chars;
    ~Utf() { if (chars) jni->ReleaseStringUTFChars(string, chars); }
  } requested_name{jni, name, jni->GetStringUTFChars(name, nullptr)};
  if (!requested_name.chars) return ServiceFailure(jni);
  if (requested_name.chars[0] == '<') return false;
  Local<jobject> type{jni};
  Local<jclass> requested_return{jni};
  Text requested_signature;
  const auto read_type = [&] {
    if (type.value) return true;
    if (environment->GetLocalObject(nullptr, static_lookup ? 3 : virtual_depth,
                                   static_lookup ? static_type_slot : type_slot, &type.value) ||
        !type.value || !jni->IsInstanceOf(type.value, method_type)) { ServiceFailure(jni); return false; }
    requested_return.value = static_cast<jclass>(jni->GetObjectField(type.value, return_field));
    if (!requested_return.value || environment->GetClassSignature(requested_return.value, &requested_signature.value, nullptr)) {
      ServiceFailure(jni); return false;
    }
    if (probing && recursive_lookup) observed_recursive = true;
    if (probing && static_lookup && !(flags & evolution::kAccInterface)) observed_static_class = true;
    return true;
  };
  const bool class_resolution = static_lookup && !(flags & evolution::kAccInterface) &&
      find_class && (probing_inherited_static || inherited_static_verified);
  Local<jclass> declaring{jni};
  if (static_lookup && ((flags & evolution::kAccInterface) || class_resolution)) {
    if (!read_type()) return true;
    std::string descriptor("(");
    for (jsize i = 0, n = jni->GetArrayLength(args); i < n; ++i) {
      Local<jclass> parameter{jni, static_cast<jclass>(jni->GetObjectArrayElement(args, i))};
      Text signature;
      if (!parameter.value || environment->GetClassSignature(parameter.value, &signature.value, nullptr)) return ServiceFailure(jni);
      descriptor += signature.value;
    }
    descriptor += ")"; descriptor += requested_signature.value;
    struct Global {
      JNIEnv* jni; jobject value;
      ~Global() { if (value) jni->DeleteGlobalRef(value); }
    } root{jni, jni->NewGlobalRef(klass)};
    if (!root.value) return ServiceFailure(jni);
    void* owner = decode_global(runtime_vm, root.value);
    if (!owner) return ServiceFailure(jni);
    void* resolver = class_resolution ? find_class : find_interface;
    auto* method = runtime_api >= 35
        ? reinterpret_cast<abi::arm64::android15::FindClassMethod>(resolver)(
            owner, requested_name.chars, descriptor, abi::arm64::android15::PointerSize::k64)
        : reinterpret_cast<abi::arm64::android11::FindClassMethod>(resolver)(
            owner, requested_name.chars, descriptor, abi::arm64::android11::PointerSize::k64);
    if (!method) {
      jni->ThrowNew(missing_error, "no method with the requested descriptor");
      return true;
    }
    // Class objects are nonmoving (Class::Alloc, fixed 11--17). Root the
    // resolved declaration through ART's JNI bridge before public inspection;
    // a boot declaration must retain libcore's hidden-API filtering.
    declaring.value = static_cast<jclass>(new_local(jni,
        reinterpret_cast<void*>(static_cast<uintptr_t>(method->declaring_class))));
    if (!declaring.value) return ServiceFailure(jni);
    Local<jobject> declaring_loader{jni};
    if (environment->GetClassLoader(declaring.value, &declaring_loader.value)) return ServiceFailure(jni);
    if (!declaring_loader.value || (jni->GetIntField(declaring.value, class_flags_field) & kAccObsoleteObject)) return false;
    if (!(method->access_flags.load(std::memory_order_relaxed) & evolution::kAccStatic)) {
      if (probing && !class_resolution) observed_static = true;
      jni->ThrowNew(access_error, "method is not static");
      return true;
    }
  }
  struct Methods {
    jmethodID* value = nullptr;
    ~Methods() { if (value) environment->Deallocate(reinterpret_cast<unsigned char*>(value)); }
  } methods;
  // ART selected the complete descriptor. Use the public method ID for that
  // declaration; never reinterpret an ArtMethod pointer as a JNI method ID.
  jclass methods_owner = class_resolution ? declaring.value : static_cast<jclass>(klass);
  if (environment->GetClassMethods(methods_owner, &count, &methods.value)) return ServiceFailure(jni);
  for (jint i = 0; i < count; ++i) {
    jint modifiers = 0;
    if (environment->GetMethodModifiers(methods.value[i], &modifiers)) return ServiceFailure(jni);
    if (recursive_lookup && !(modifiers & evolution::kAccPublic)) continue;
    if (virtual_lookup && (modifiers & evolution::kAccStatic) && !(flags & evolution::kAccInterface)) continue;
    Text member_name, signature;
    if (environment->GetMethodName(methods.value[i], &member_name.value, &signature.value, nullptr)) return ServiceFailure(jni);
    if (std::strcmp(member_name.value, requested_name.chars)) continue;
    if (!read_type()) return true;
    const size_t end = ReturnOffset(signature.value);
    if (end == std::string_view::npos || std::strcmp(signature.value + end, requested_signature.value)) continue;
    size_t at = 1;
    const jsize parameters = jni->GetArrayLength(args);
    for (jsize parameter = 0; parameter < parameters && at < end; ++parameter) {
      Local<jclass> klass_parameter{jni, static_cast<jclass>(jni->GetObjectArrayElement(args, parameter))};
      Text descriptor;
      if (!klass_parameter.value || environment->GetClassSignature(klass_parameter.value, &descriptor.value, nullptr)) return ServiceFailure(jni);
      const size_t size = std::strlen(descriptor.value);
      if (at + size >= end || std::strncmp(signature.value + at, descriptor.value, size)) { at = end; break; }
      at += size;
    }
    if (at + 1 != end) continue;
    if ((modifiers & evolution::kAccStatic) && virtual_lookup) {
      if (probing) observed = true;
      jni->ThrowNew(access_error, "interface method is static");
      return true;
    }
    // Use ART's reflection conversion for actual Class identities; do not
    // resolve descriptor strings through a second class-loader search.
    Local<jobject> reflected{jni, jni->ToReflectedMethod(methods_owner, methods.value[i], (modifiers & evolution::kAccStatic) ? JNI_TRUE : JNI_FALSE)};
    if (!reflected.value || jni->ExceptionCheck()) return ServiceFailure(jni);
    Local<jobject> actual_return{jni, jni->CallObjectMethod(reflected.value, reflected_return)};
    if (!actual_return.value || jni->ExceptionCheck()) return ServiceFailure(jni);
    bool aliases = !jni->IsSameObject(requested_return.value, actual_return.value);
    Local<jobjectArray> actual_parameters{jni, static_cast<jobjectArray>(jni->CallObjectMethod(reflected.value, reflected_parameters))};
    if (!actual_parameters.value || jni->ExceptionCheck()) return ServiceFailure(jni);
    if (jni->GetArrayLength(actual_parameters.value) != parameters) return ServiceFailure(jni);
    for (jsize parameter = 0; parameter < parameters; ++parameter) {
      Local<jobject> requested_parameter{jni, jni->GetObjectArrayElement(args, parameter)};
      Local<jobject> actual_parameter{jni, jni->GetObjectArrayElement(actual_parameters.value, parameter)};
      if (!actual_parameter.value || jni->ExceptionCheck()) return ServiceFailure(jni);
      aliases |= !jni->IsSameObject(requested_parameter.value, actual_parameter.value);
    }
    if (aliases) {
      jni->ThrowNew(access_error, "method type has a different class-loader identity");
      return true;
    }
    const bool inherited = class_resolution && !jni->IsSameObject(klass, declaring.value);
    if (class_resolution && probing_nonpublic_static && !(flags & evolution::kAccPublic))
      observed_nonpublic_static |= inherited ? 2u : 1u;
    if (inherited) {
      // Return the genuine reflected declaration to findStatic. Its existing
      // return-type and lookup-owner access checks still run before a handle
      // is created. Ordinary getDeclaredMethod never enters this stack chain.
      if (probing_inherited_static) observed_inherited_static = true;
      *result = reflected.value;
      reflected.value = nullptr;
      return true;
    }
    if (class_resolution) return false;  // Matching declared methods retain the original entry.
  }
  return class_resolution ? ServiceFailure(jni) : false;
}
jobject DeclaredMethod(JNIEnv* jni, jobject klass, jstring name, jobjectArray args) noexcept try {
  jobject result = nullptr;
  if ((probing || enabled.load(std::memory_order_acquire)) && !jni->ExceptionCheck() &&
      ResolveLookup(jni, klass, name, args, &result)) return result;
  return original(jni, klass, name, args);
} catch (...) {
  ServiceFailure(jni);
  return nullptr;
}
jmethodID Method(jclass klass, const char* name, const char* signature) {
  jint count = 0;
  jmethodID* methods = nullptr;
  jmethodID found = nullptr;
  if (environment->GetClassMethods(klass, &count, &methods)) return nullptr;
  for (jint i = 0; i < count; ++i) {
    Text n, s;
    if (!environment->GetMethodName(methods[i], &n.value, &s.value, nullptr) &&
        !std::strcmp(n.value, name) && !std::strcmp(s.value, signature)) { found = methods[i]; break; }
  }
  environment->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return found;
}
jfieldID Field(jclass klass, const char* name, const char* signature) {
  jint count = 0;
  jfieldID* fields = nullptr;
  jfieldID found = nullptr;
  if (environment->GetClassFields(klass, &count, &fields)) return nullptr;
  for (jint i = 0; i < count; ++i) {
    Text n, s;
    if (!environment->GetFieldName(klass, fields[i], &n.value, &s.value, nullptr) &&
        !std::strcmp(n.value, name) && !std::strcmp(s.value, signature)) { found = fields[i]; break; }
  }
  environment->Deallocate(reinterpret_cast<unsigned char*>(fields));
  return found;
}
jclass GlobalClass(JNIEnv* jni, const char* name) {
  Local<jclass> local{jni, jni->FindClass(name)};
  return local.value ? static_cast<jclass>(jni->NewGlobalRef(local.value)) : nullptr;
}
}  // namespace
bool Prepare(JNIEnv* jni, jvmtiEnv* env) {
  if (Available()) return true;
  if (preparing) return false; // Same-thread callbacks may nest another routed batch.
  struct Preparing { Preparing() { preparing = true; } ~Preparing() { preparing = false; } } scope;
  if (hook) return false;  // A failed probe unhook must never be installed over itself.
  using namespace symbols::method_handle_lookup;
  target = symbols::Find(kDeclaredMethod);
  if (!target) target = symbols::Find(kDeclaredMethodUnique);
  if (!target || !jni || !env) return false;
  jvmtiCapabilities capabilities{};
  capabilities.can_access_local_variables = 1;
  if (env->AddCapabilities(&capabilities)) return false;
  environment = env;
  find_interface = symbols::Find(kFindInterfaceMethod);
  find_class = symbols::Find(kFindClassMethod);
  runtime_api = android_get_device_api_level();
  decode_global = reinterpret_cast<decltype(decode_global)>(symbols::Find(kDecodeGlobal));
  new_local = reinterpret_cast<decltype(new_local)>(symbols::Find(kNewLocalRef));
  if (jni->GetJavaVM(&runtime_vm)) runtime_vm = nullptr;
  Local<jclass> klass{jni, jni->FindClass("java/lang/Class")};
  if (!klass.value) { jni->ExceptionClear(); return false; }
  Local<jclass> lookup{jni, jni->FindClass("java/lang/invoke/MethodHandles$Lookup")};
  if (!lookup.value) { jni->ExceptionClear(); return false; }
  Local<jclass> reflected{jni, jni->FindClass("java/lang/reflect/Method")};
  if (!reflected.value) { jni->ExceptionClear(); return false; }
  reflected_return = jni->GetMethodID(reflected.value, "getReturnType", "()Ljava/lang/Class;");
  reflected_parameters = jni->GetMethodID(reflected.value, "getParameterTypes", "()[Ljava/lang/Class;");
  if (!method_type) method_type = GlobalClass(jni, "java/lang/invoke/MethodType");
  if (!method_type) { jni->ExceptionClear(); return false; }
  if (!access_error) access_error = GlobalClass(jni, "java/lang/IllegalAccessException");
  if (!access_error) { jni->ExceptionClear(); return false; }
  if (!internal_error) internal_error = GlobalClass(jni, "java/lang/InternalError");
  if (!internal_error) { jni->ExceptionClear(); return false; }
  if (!missing_error) missing_error = GlobalClass(jni, "java/lang/NoSuchMethodException");
  if (!missing_error) { jni->ExceptionClear(); return false; }
  {
    instance_method = Method(klass.value, "getInstanceMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    public_recursive = Method(klass.value, "getPublicMethodRecursive", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    interface_recursive = Method(klass.value, "findInterfaceMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    find_virtual = Method(lookup.value, "findVirtual", "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    find_static = Method(lookup.value, "findStatic", "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    declared_method = Method(klass.value, "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    class_get_method = Method(klass.value, "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;Z)Ljava/lang/reflect/Method;");
    return_field = Field(method_type, "rtype", "Ljava/lang/Class;");
    class_flags_field = Field(klass.value, "accessFlags", "I");
    jint locals = 0;
    // All fixed libcore versions retain the last incoming argument (MethodType).
    if (find_virtual && !env->GetMaxLocals(find_virtual, &locals) && locals >= 4) type_slot = locals - 1;
    if (find_static && !env->GetMaxLocals(find_static, &locals) && locals >= 4) static_type_slot = locals - 1;
  }
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  if (!return_field || !class_flags_field || !instance_method || !public_recursive || !interface_recursive || !find_virtual || !reflected_return || !reflected_parameters || type_slot < 0) return false;
  Local<jclass> probe{jni, jni->FindClass("com/github/ytlog/artdcevm/internal/MethodHandleLookupProbe")};
  jmethodID run = probe.value ? jni->GetStaticMethodID(probe.value, "run", "()V") : nullptr;
  if (!run || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  // One Native-phase test before ART takes its redefinition/linker locks.
  // A stock IAE alone is insufficient: the hook must observe the actual live type.
  {
    std::lock_guard<std::mutex> lock(install_mutex);
    hook = shadowhook_hook_sym_addr(target, HookAddress(DeclaredMethod), HookSlot(&original));
  }
  if (!hook) return false;
  observed = observed_recursive = false;
  probing = true;
  jni->CallStaticVoidMethod(probe.value, run);
  probing = false;
  const bool passed = observed && observed_recursive && !jni->ExceptionCheck();
  if (jni->ExceptionCheck()) jni->ExceptionClear();
  // Optional static resolution proof must not disable the existing virtual guard.
  static_verified = false;
  jmethodID run_static = jni->GetStaticMethodID(probe.value, "runStatic", "()V");
  if (jni->ExceptionCheck()) jni->ExceptionClear();
  if (passed && run_static && runtime_vm && find_interface && decode_global && new_local && find_static && declared_method && class_get_method && static_type_slot >= 0) {
    observed_static = observed_static_class = false; probing = true;
    jni->CallStaticVoidMethod(probe.value, run_static);
    probing = false;
    static_verified = observed_static && observed_static_class && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  // A separately observed inherited declaration and actual call are required;
  // absence/failure preserves the independently proved existing static path.
  inherited_static_verified = false;
  jmethodID run_inherited = jni->GetStaticMethodID(probe.value, "runInheritedStatic", "()V");
  if (jni->ExceptionCheck()) jni->ExceptionClear();
  if (static_verified && find_class && run_inherited) {
    observed_inherited_static = false;
    probing = probing_inherited_static = true;
    jni->CallStaticVoidMethod(probe.value, run_inherited);
    probing = probing_inherited_static = false;
    inherited_static_verified = observed_inherited_static && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  // Nonpublic class resolution must separately prove both native paths and
  // actual allowed calls plus libcore's denied lookup. Never grant access here.
  nonpublic_static_verified = false;
  jmethodID run_nonpublic = jni->GetStaticMethodID(probe.value, "runNonpublicStatic", "()V");
  if (jni->ExceptionCheck()) jni->ExceptionClear();
  if (inherited_static_verified && run_nonpublic) {
    observed_nonpublic_static = 0;
    probing = probing_nonpublic_static = true;
    jni->CallStaticVoidMethod(probe.value, run_nonpublic);
    probing = probing_nonpublic_static = false;
    nonpublic_static_verified = observed_nonpublic_static == 3 && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  {
    std::lock_guard<std::mutex> lock(install_mutex);
    if (shadowhook_unhook(hook)) return false;
    hook = nullptr;
  }
  probe_verified = passed;
  return Available();
}
bool Available() {
  return probe_verified && target && environment && method_type && access_error && internal_error && return_field && class_flags_field &&
      instance_method && find_virtual && reflected_return && reflected_parameters && type_slot >= 0;
}
bool Enable() {
  if (!Available()) return false;
  std::lock_guard<std::mutex> lock(install_mutex);
  if (!hook) hook = shadowhook_hook_sym_addr(target, HookAddress(DeclaredMethod), HookSlot(&original));
  if (!hook) return false;
  enabled.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::method_handle_lookup

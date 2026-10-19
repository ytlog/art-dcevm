#include "platform/common/method_handle_lookup.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include "platform/common/art_hook.h"
#include "platform/symbols/method_handle_lookup_symbols.h"

namespace artdcevm::method_handle_lookup {
namespace {
using Lookup = jobject (*)(JNIEnv*, jobject, jstring, jobjectArray);
Lookup original = nullptr;
void* target = nullptr;
void* hook = nullptr;
jvmtiEnv* environment = nullptr;
jclass method_type = nullptr, access_error = nullptr, internal_error = nullptr;
jfieldID return_field = nullptr, class_flags_field = nullptr;
jmethodID instance_method = nullptr, find_virtual = nullptr;
jint type_slot = -1;
std::atomic<bool> enabled{false};
bool probe_verified = false;
thread_local bool probing = false, observed = false, preparing = false;
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
// Only an exact own static declaration can make this decision. ART still owns
// every inherited/instance lookup; public JVMTI supplies declared member IDs.
// Compare descriptors before libcore resolves parameter Class identities, as
// JBR rejects the invocation kind before same-name loader aliasing.
bool Reject(JNIEnv* jni, jobject klass, jstring name, jobjectArray args) {
  if (!name || !args) return false;  // Preserve the original null checks.
  const jint flags = jni->GetIntField(klass, class_flags_field);
  // dex/modifiers.h, fixed 11--17: leave obsolete mirrors to the original check.
  constexpr jint kAccObsoleteObject = 0x00200000;
  if (!(flags & evolution::kAccInterface) || (flags & kAccObsoleteObject)) return false;
  jvmtiFrameInfo frames[3]{};
  jint count = 0;
  if (environment->GetStackTrace(nullptr, 0, 3, frames, &count)) return ServiceFailure(jni);
  if (count != 3 || frames[1].method != instance_method || frames[2].method != find_virtual) return false;
  Local<jobject> loader{jni};
  if (environment->GetClassLoader(static_cast<jclass>(klass), &loader.value)) return ServiceFailure(jni);
  if (!loader.value) return false; // Boot interfaces cannot be enhanced; keep hidden-API filtering.
  struct Utf {
    JNIEnv* jni; jstring string; const char* chars;
    ~Utf() { if (chars) jni->ReleaseStringUTFChars(string, chars); }
  } requested_name{jni, name, jni->GetStringUTFChars(name, nullptr)};
  if (!requested_name.chars) return ServiceFailure(jni);
  if (requested_name.chars[0] == '<') return false;
  struct Methods {
    jmethodID* value = nullptr;
    ~Methods() { if (value) environment->Deallocate(reinterpret_cast<unsigned char*>(value)); }
  } methods;
  if (environment->GetClassMethods(static_cast<jclass>(klass), &count, &methods.value)) return ServiceFailure(jni);
  Local<jobject> type{jni};
  Local<jclass> requested_return{jni};
  Text requested_signature;
  for (jint i = 0; i < count; ++i) {
    jint modifiers = 0;
    if (environment->GetMethodModifiers(methods.value[i], &modifiers)) return ServiceFailure(jni);
    if (!(modifiers & evolution::kAccStatic)) continue;
    Text member_name, signature;
    if (environment->GetMethodName(methods.value[i], &member_name.value, &signature.value, nullptr)) return ServiceFailure(jni);
    if (std::strcmp(member_name.value, requested_name.chars)) continue;
    if (!type.value) {
      if (environment->GetLocalObject(nullptr, 2, type_slot, &type.value) || !type.value ||
          !jni->IsInstanceOf(type.value, method_type)) return ServiceFailure(jni);
      requested_return.value = static_cast<jclass>(jni->GetObjectField(type.value, return_field));
      if (!requested_return.value || environment->GetClassSignature(requested_return.value, &requested_signature.value, nullptr)) return ServiceFailure(jni);
    }
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
    if (probing) observed = true;
    jni->ThrowNew(access_error, "interface method is static");
    return true;
  }
  return false;
}
jobject DeclaredMethod(JNIEnv* jni, jobject klass, jstring name, jobjectArray args) noexcept {
  if ((probing || enabled.load(std::memory_order_acquire)) && !jni->ExceptionCheck() && Reject(jni, klass, name, args)) return nullptr;
  return original(jni, klass, name, args);
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
  Local<jclass> klass{jni, jni->FindClass("java/lang/Class")};
  if (!klass.value) { jni->ExceptionClear(); return false; }
  Local<jclass> lookup{jni, jni->FindClass("java/lang/invoke/MethodHandles$Lookup")};
  if (!lookup.value) { jni->ExceptionClear(); return false; }
  if (!method_type) method_type = GlobalClass(jni, "java/lang/invoke/MethodType");
  if (!method_type) { jni->ExceptionClear(); return false; }
  if (!access_error) access_error = GlobalClass(jni, "java/lang/IllegalAccessException");
  if (!access_error) { jni->ExceptionClear(); return false; }
  if (!internal_error) internal_error = GlobalClass(jni, "java/lang/InternalError");
  if (!internal_error) { jni->ExceptionClear(); return false; }
  {
    instance_method = Method(klass.value, "getInstanceMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    find_virtual = Method(lookup.value, "findVirtual", "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    return_field = Field(method_type, "rtype", "Ljava/lang/Class;");
    class_flags_field = Field(klass.value, "accessFlags", "I");
    jint locals = 0;
    // All fixed libcore versions retain the last incoming argument (MethodType).
    if (find_virtual && !env->GetMaxLocals(find_virtual, &locals) && locals >= 4) type_slot = locals - 1;
  }
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  if (!return_field || !class_flags_field || !instance_method || !find_virtual || type_slot < 0) return false;
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
  observed = false;
  probing = true;
  jni->CallStaticVoidMethod(probe.value, run);
  probing = false;
  const bool passed = observed && !jni->ExceptionCheck();
  if (jni->ExceptionCheck()) jni->ExceptionClear();
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
      instance_method && find_virtual && type_slot >= 0;
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

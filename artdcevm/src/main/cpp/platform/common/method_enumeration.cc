#include "platform/common/method_enumeration.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "platform/common/art_hook.h"
#include "platform/symbols/method_enumeration_symbols.h"

namespace artdcevm::method_enumeration {
namespace {
using Entry = jobjectArray (*)(JNIEnv*, jobject, jboolean);
Entry original = nullptr;
void* target = nullptr;
void* hook = nullptr;
jvmtiEnv* environment = nullptr;
jclass list_class = nullptr, error_class = nullptr, method_class = nullptr;
jmethodID collect = nullptr, get_methods = nullptr;
jmethodID list_size = nullptr, list_get = nullptr, list_set = nullptr;
jmethodID declaring = nullptr, name = nullptr, return_type = nullptr, parameters = nullptr, modifiers = nullptr;
jint list_slot = -1;
bool verified = false, static_verified = false, bootstrap_verified = false, override_verified = false;
std::atomic<bool> enabled{false}, static_enabled{false}, bootstrap_enabled{false}, override_enabled{false};
std::mutex install_mutex;
thread_local bool preparing = false, probing = false, processing = false;
thread_local unsigned observed = 0, static_observed = 0, bootstrap_observed = 0, override_observed = 0;
thread_local bool static_probing = false, bootstrap_probing = false, override_probing = false;
constexpr jint kPublic = 0x0001, kStatic = 0x0008, kBridge = 0x0040, kInterface = 0x0200;

template<class T> struct Local {
  JNIEnv* jni;
  T value{};
  ~Local() { if (value) jni->DeleteLocalRef(value); }
};
bool Failure(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(error_class, "public method enumeration service unavailable");
  return false;
}
bool AppClass(JNIEnv* jni, jclass klass, bool* app) {
  Local<jobject> loader{jni};
  if (environment->GetClassLoader(klass, &loader.value)) return Failure(jni);
  *app = loader.value != nullptr;
  return true;
}
struct Method {
  JNIEnv* jni;
  Local<jclass> owner;
  Local<jobject> result;
  Local<jobjectArray> args;
  std::string method_name;
  jint flags = 0;
  explicit Method(JNIEnv* env) : jni(env), owner{env}, result{env}, args{env} {}
  bool Read(jobject method) {
    flags = jni->CallIntMethod(method, modifiers);
    if (jni->ExceptionCheck()) return false;
    owner.value = static_cast<jclass>(jni->CallObjectMethod(method, declaring));
    if (!owner.value || jni->ExceptionCheck()) return Failure(jni);
    return true;
  }
  bool Signature(jobject method) {
    if (result.value) return true;
    Local<jstring> text{jni, static_cast<jstring>(jni->CallObjectMethod(method, name))};
    if (!text.value || jni->ExceptionCheck()) return Failure(jni);
    const char* utf = jni->GetStringUTFChars(text.value, nullptr);
    if (!utf) return Failure(jni);
    struct Utf { JNIEnv* jni; jstring text; const char* value;
      ~Utf() { jni->ReleaseStringUTFChars(text, value); } } chars{jni, text.value, utf};
    method_name = utf;
    result.value = jni->CallObjectMethod(method, return_type);
    if (!result.value || jni->ExceptionCheck()) return Failure(jni);
    args.value = static_cast<jobjectArray>(jni->CallObjectMethod(method, parameters));
    return args.value && !jni->ExceptionCheck() ? true : Failure(jni);
  }
};
bool SameSignature(JNIEnv* jni, const Method& a, const Method& b) {
  if (a.method_name != b.method_name || !jni->IsSameObject(a.result.value, b.result.value)) return false;
  const jsize count = jni->GetArrayLength(a.args.value);
  if (count != jni->GetArrayLength(b.args.value)) return false;
  for (jsize i = 0; i < count; ++i) {
    Local<jobject> x{jni, jni->GetObjectArrayElement(a.args.value, i)};
    if (jni->ExceptionCheck()) return false;
    Local<jobject> y{jni, jni->GetObjectArrayElement(b.args.value, i)};
    if (jni->ExceptionCheck()) return false;
    if (!jni->IsSameObject(x.value, y.value)) return false;
  }
  return true;
}
bool Refine(JNIEnv* jni, jobject klass, jobjectArray* methods) {
  jint flags = 0;
  if (environment->GetClassModifiers(static_cast<jclass>(klass), &flags)) return Failure(jni);
  if (!(flags & kInterface)) return true;
  bool app = false;
  if (!AppClass(jni, static_cast<jclass>(klass), &app)) return false;
  const bool candidate_app = app;
  if (!candidate_app && !(bootstrap_probing || bootstrap_enabled.load(std::memory_order_acquire))) return true;
  jvmtiFrameInfo frames[3]{};
  jint count = 0;
  if (environment->GetStackTrace(nullptr, 0, 3, frames, &count)) return Failure(jni);
  if (count != 3 || frames[1].method != collect || frames[2].method != get_methods) return true;
  Local<jobject> root{jni}, list{jni};
  // Fixed Class.java: getPublicMethodsInternal(List) retains the list as its
  // last incoming argument. Prove both live values before publishing the hook.
  if (environment->GetLocalInstance(nullptr, 1, &root.value) || !root.value) return Failure(jni);
  if (!AppClass(jni, static_cast<jclass>(root.value), &app)) return false;
  if (!app) {
    if (bootstrap_probing && !candidate_app && jni->IsSameObject(root.value, klass)) bootstrap_observed |= 4u;
    return true; // Preserve bootstrap roots before reading the application collection.
  }
  if (environment->GetLocalObject(nullptr, 1, list_slot, &list.value) || !list.value ||
      !jni->IsInstanceOf(list.value, list_class)) return Failure(jni);
  jint root_flags = 0;
  if (environment->GetClassModifiers(static_cast<jclass>(root.value), &root_flags)) return Failure(jni);
  const jint size = jni->CallIntMethod(list.value, list_size);
  if (jni->ExceptionCheck()) return false;
  const bool filter_static = static_probing || bootstrap_probing || static_enabled.load(std::memory_order_acquire);
  const bool inherited = !jni->IsSameObject(root.value, klass);
  const jsize length = jni->GetArrayLength(*methods);
  std::vector<jsize> retained;
  for (jsize m = 0; m < length; ++m) {
    Local<jobject> next{jni, jni->GetObjectArrayElement(*methods, m)};
    if (!next.value || jni->ExceptionCheck()) return Failure(jni);
    Method candidate(jni);
    if (!candidate.Read(next.value)) return false;
    if (filter_static && (candidate.flags & kStatic)) {
      if (inherited) {
        if (static_probing) static_observed |= root_flags & kInterface ? 1u : 2u;
        if (bootstrap_probing && !candidate_app) bootstrap_observed |= root_flags & kInterface ? 1u : 2u;
        continue;
      }
      if (static_probing) static_observed |= 4u;
    }
    if (filter_static) retained.push_back(m);
    if (!candidate_app || !(candidate.flags & kPublic) || (candidate.flags & kStatic)) continue;
    for (jint i = 0; i < size; ++i) {
      Local<jobject> previous{jni, jni->CallObjectMethod(list.value, list_get, i)};
      if (!previous.value || jni->ExceptionCheck()) return Failure(jni);
      Method existing(jni);
      if (!existing.Read(previous.value)) return false;
      if ((existing.flags & kStatic) || jni->IsSameObject(candidate.owner.value, existing.owner.value)) continue;
      jint old_flags = 0;
      if (environment->GetClassModifiers(existing.owner.value, &old_flags)) return Failure(jni);
      // Class declarations dominate interfaces. Unrelated interfaces keep the
      // original deduplication policy; only a strict ancestor is replaced.
      if (!(old_flags & kInterface) || !jni->IsAssignableFrom(candidate.owner.value, existing.owner.value)) continue;
      if (!AppClass(jni, existing.owner.value, &app)) return false;
      if (!app && !(override_probing || override_enabled.load(std::memory_order_acquire))) continue;
      if (!candidate.Signature(next.value) || !existing.Signature(previous.value)) return false;
      if (!SameSignature(jni, candidate, existing)) {
        if (jni->ExceptionCheck()) return false;
        continue;
      }
      Local<jobject> displaced{jni, jni->CallObjectMethod(list.value, list_set, i, next.value)};
      if (jni->ExceptionCheck()) return false;
      if (override_probing && !app) override_observed |= root_flags & kInterface
          ? candidate.flags & kBridge ? 1u : 4u : candidate.flags & kBridge ? 2u : 8u;
      if (probing) observed |= root_flags & kInterface
          ? candidate.flags & kBridge ? 1u : 4u : candidate.flags & kBridge ? 2u : 8u;
    }
  }
  if (filter_static && retained.size() != static_cast<size_t>(length)) {
    // Keep ART's Method objects and order. Only the inherited public view gets
    // a new array; own declarations and ordinary declared enumeration retain it.
    Local<jobjectArray> filtered{jni, jni->NewObjectArray(static_cast<jsize>(retained.size()), method_class, nullptr)};
    if (!filtered.value || jni->ExceptionCheck()) return Failure(jni);
    for (jsize i = 0; i < static_cast<jsize>(retained.size()); ++i) {
      Local<jobject> method{jni, jni->GetObjectArrayElement(*methods, retained[i])};
      if (!method.value || jni->ExceptionCheck()) return Failure(jni);
      jni->SetObjectArrayElement(filtered.value, i, method.value);
      if (jni->ExceptionCheck()) return false;
    }
    if (static_probing) static_observed |= retained.empty() ? 8u : 16u;
    jni->DeleteLocalRef(*methods);
    *methods = filtered.value;
    filtered.value = nullptr;
  }
  return true;
}
jobjectArray DeclaredMethods(JNIEnv* jni, jobject klass, jboolean public_only) noexcept try {
  jobjectArray result = original(jni, klass, public_only);
  if (!result || jni->ExceptionCheck() || public_only == JNI_FALSE || processing ||
      !(probing || enabled.load(std::memory_order_acquire))) return result;
  struct Processing { Processing() { processing = true; } ~Processing() { processing = false; } } scope;
  if (Refine(jni, klass, &result)) return result;
  jni->DeleteLocalRef(result);
  return nullptr;
} catch (...) {
  Failure(jni);
  return nullptr;
}
jmethodID Identity(jclass klass, const char* wanted, const char* signature) {
  jint count = 0; jmethodID* methods = nullptr; jmethodID result = nullptr;
  if (environment->GetClassMethods(klass, &count, &methods)) return nullptr;
  for (jint i = 0; i < count; ++i) {
    char* n = nullptr; char* s = nullptr;
    if (!environment->GetMethodName(methods[i], &n, &s, nullptr) &&
        !std::strcmp(n, wanted) && !std::strcmp(s, signature)) result = methods[i];
    if (n) environment->Deallocate(reinterpret_cast<unsigned char*>(n));
    if (s) environment->Deallocate(reinterpret_cast<unsigned char*>(s));
  }
  environment->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return result;
}
}
bool Prepare(JNIEnv* jni, jvmtiEnv* env) {
  if (verified) return true;
  if (preparing || hook || !jni || !env) return false;
  struct Preparing { Preparing() { preparing = true; } ~Preparing() { preparing = false; } } scope;
  environment = env;
  target = symbols::Find(symbols::method_enumeration::kDeclaredMethods);
  if (!target) target = symbols::Find(symbols::method_enumeration::kDeclaredMethodsUnique);
  if (!target) return false;
  jvmtiCapabilities caps{}; caps.can_access_local_variables = 1;
  if (env->AddCapabilities(&caps)) return false;
  Local<jclass> klass{jni, jni->FindClass("java/lang/Class")};
  if (!klass.value || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  Local<jclass> method{jni, jni->FindClass("java/lang/reflect/Method")};
  if (!method.value || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  Local<jclass> list{jni, jni->FindClass("java/util/List")};
  if (!list.value || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  Local<jclass> error{jni, jni->FindClass("java/lang/InternalError")};
  if (!error.value || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  Local<jclass> probe{jni, jni->FindClass("com/github/ytlog/artdcevm/internal/MethodEnumerationProbe")};
  if (jni->ExceptionCheck() || !klass.value || !method.value || !list.value || !error.value || !probe.value) {
    jni->ExceptionClear(); return false;
  }
  if (!list_class) list_class = static_cast<jclass>(jni->NewGlobalRef(list.value));
  if (!list_class || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  if (!error_class) error_class = static_cast<jclass>(jni->NewGlobalRef(error.value));
  if (!error_class || jni->ExceptionCheck()) { jni->ExceptionClear(); return false; }
  collect = Identity(klass.value, "getPublicMethodsInternal", "(Ljava/util/List;)V");
  get_methods = Identity(klass.value, "getMethods", "()[Ljava/lang/reflect/Method;");
  jint locals = 0;
  if (collect && !env->GetMaxLocals(collect, &locals) && locals >= 2) list_slot = locals - 1;
  jmethodID run = nullptr;
  const bool methods_ready =
      (list_size = jni->GetMethodID(list.value, "size", "()I")) && !jni->ExceptionCheck() &&
      (list_get = jni->GetMethodID(list.value, "get", "(I)Ljava/lang/Object;")) && !jni->ExceptionCheck() &&
      (list_set = jni->GetMethodID(list.value, "set", "(ILjava/lang/Object;)Ljava/lang/Object;")) && !jni->ExceptionCheck() &&
      (declaring = jni->GetMethodID(method.value, "getDeclaringClass", "()Ljava/lang/Class;")) && !jni->ExceptionCheck() &&
      (name = jni->GetMethodID(method.value, "getName", "()Ljava/lang/String;")) && !jni->ExceptionCheck() &&
      (return_type = jni->GetMethodID(method.value, "getReturnType", "()Ljava/lang/Class;")) && !jni->ExceptionCheck() &&
      (parameters = jni->GetMethodID(method.value, "getParameterTypes", "()[Ljava/lang/Class;")) && !jni->ExceptionCheck() &&
      (modifiers = jni->GetMethodID(method.value, "getModifiers", "()I")) && !jni->ExceptionCheck() &&
      (run = jni->GetStaticMethodID(probe.value, "run", "()V")) && !jni->ExceptionCheck();
  if (jni->ExceptionCheck() || !list_class || !error_class || !collect || !get_methods || list_slot < 0 ||
      !list_size || !list_get || !list_set || !declaring || !name || !return_type || !parameters || !modifiers || !run || !methods_ready) {
    jni->ExceptionClear(); return false;
  }
  // Optional proof: failure must not disable the already supported specificity.
  jmethodID run_static = jni->GetStaticMethodID(probe.value, "runStatic", "()V");
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); run_static = nullptr; }
  if (run_static && !method_class) method_class = static_cast<jclass>(jni->NewGlobalRef(method.value));
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); run_static = nullptr; }
  jmethodID run_bootstrap = jni->GetStaticMethodID(probe.value, "runBootstrapStatic", "()V");
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); run_bootstrap = nullptr; }
  jmethodID run_override = jni->GetStaticMethodID(probe.value, "runBootstrapOverride", "()V");
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); run_override = nullptr; }
  { std::lock_guard<std::mutex> lock(install_mutex);
    hook = shadowhook_hook_sym_addr(target, HookAddress(DeclaredMethods), HookSlot(&original)); }
  if (!hook) return false;
  observed = 0; probing = true;
  jni->CallStaticVoidMethod(probe.value, run);
  probing = false;
  const bool passed = observed == 15 && !jni->ExceptionCheck();
  if (jni->ExceptionCheck()) jni->ExceptionClear();
  static_verified = false;
  if (passed && run_static && method_class) {
    static_observed = 0; probing = true; static_probing = true;
    jni->CallStaticVoidMethod(probe.value, run_static);
    static_probing = false; probing = false;
    static_verified = static_observed == 31 && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  bootstrap_verified = false;
  if (static_verified && run_bootstrap) {
    bootstrap_observed = 0; probing = true; bootstrap_probing = true;
    jni->CallStaticVoidMethod(probe.value, run_bootstrap);
    bootstrap_probing = false; probing = false;
    bootstrap_verified = bootstrap_observed == 7 && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  override_verified = false;
  if (passed && run_override) {
    override_observed = 0; probing = true; override_probing = true;
    jni->CallStaticVoidMethod(probe.value, run_override);
    override_probing = false; probing = false;
    override_verified = override_observed == 15 && !jni->ExceptionCheck();
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
  { std::lock_guard<std::mutex> lock(install_mutex);
    if (shadowhook_unhook(hook)) return false;
    hook = nullptr; }
  verified = passed;
  return verified;
}
bool Enable() {
  if (!verified) return false;
  std::lock_guard<std::mutex> lock(install_mutex);
  if (!hook) hook = shadowhook_hook_sym_addr(target, HookAddress(DeclaredMethods), HookSlot(&original));
  if (!hook) return false;
  override_enabled.store(override_verified, std::memory_order_release);
  bootstrap_enabled.store(bootstrap_verified, std::memory_order_release);
  static_enabled.store(static_verified, std::memory_order_release);
  enabled.store(true, std::memory_order_release);
  return true;
}
}

#include "platform/common/bound_method_handle.h"
#include <atomic>
#include <cstring>
#include "evolution/modifiers.h"

namespace artdcevm::bound_method_handle {
namespace {
std::atomic<bool> ready{false}, preparing{false}, enabled{false};
std::atomic<jmethodID> transform{nullptr};
jfieldID delegate_field = nullptr, receiver_field = nullptr, kind_field = nullptr;
jfieldID type_field = nullptr, parameters_field = nullptr;
jclass handles_class = nullptr, method_class = nullptr, incompatible_class = nullptr, internal_class = nullptr;
jmethodID reflect_as = nullptr;
thread_local bool probing = false, observed = false, checking = false;
struct Locals {
  JNIEnv* jni;
  ~Locals() { jni->PopLocalFrame(nullptr); }
};
jclass Global(JNIEnv* jni, const char* name) {
  jclass local = jni->FindClass(name);
  return local ? static_cast<jclass>(jni->NewGlobalRef(local)) : nullptr;
}
void Failed(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(internal_class, "Bound MethodHandle receiver inspection failed");
}
jfieldID Field(jvmtiEnv* env, jclass klass, const char* name, const char* signature) {
  jint count = 0;
  jfieldID* fields = nullptr;
  if (env->GetClassFields(klass, &count, &fields)) return nullptr;
  jfieldID result = nullptr;
  for (jint i = 0; i < count; ++i) {
    char* found_name = nullptr;
    char* found_signature = nullptr;
    if (!env->GetFieldName(klass, fields[i], &found_name, &found_signature, nullptr) &&
        std::strcmp(found_name, name) == 0 && std::strcmp(found_signature, signature) == 0) result = fields[i];
    if (found_name) env->Deallocate(reinterpret_cast<unsigned char*>(found_name));
    if (found_signature) env->Deallocate(reinterpret_cast<unsigned char*>(found_signature));
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(fields));
  return result;
}
jmethodID Transform(jvmtiEnv* env, jclass klass) {
  jint count = 0;
  jmethodID* methods = nullptr;
  if (env->GetClassMethods(klass, &count, &methods)) return nullptr;
  jmethodID result = nullptr;
  for (jint i = 0; i < count; ++i) {
    char* name = nullptr;
    char* signature = nullptr;
    if (!env->GetMethodName(methods[i], &name, &signature, nullptr) &&
        std::strcmp(name, "transform") == 0 && std::strcmp(signature, "(Ldalvik/system/EmulatedStackFrame;)V") == 0) result = methods[i];
    if (name) env->Deallocate(reinterpret_cast<unsigned char*>(name));
    if (signature) env->Deallocate(reinterpret_cast<unsigned char*>(signature));
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return result;
}
void Check(jvmtiEnv* env, JNIEnv* jni, jthread thread) {
  jobject bound = nullptr;
  if (env->GetLocalInstance(thread, 0, &bound) || !bound) { Failed(jni); return; }
  jobject delegate = jni->GetObjectField(bound, delegate_field);
  if (jni->ExceptionCheck()) return;
  jobject receiver = jni->GetObjectField(bound, receiver_field);
  if (jni->ExceptionCheck() || !delegate || !receiver) return;
  const jint kind = jni->GetIntField(delegate, kind_field);
  if (jni->ExceptionCheck()) return;
  if (kind != 0 && kind != 4) return; // Fixed MethodHandle INVOKE_VIRTUAL / INVOKE_INTERFACE.
  jobject type = jni->GetObjectField(delegate, type_field);
  if (jni->ExceptionCheck()) return;
  auto parameters = type ? static_cast<jobjectArray>(jni->GetObjectField(type, parameters_field)) : nullptr;
  if (jni->ExceptionCheck()) return;
  if (!parameters || !jni->GetArrayLength(parameters)) { Failed(jni); return; }
  auto receiver_type = static_cast<jclass>(jni->GetObjectArrayElement(parameters, 0));
  if (!receiver_type) { Failed(jni); return; }
  if (jni->IsInstanceOf(receiver, receiver_type)) return;
  jint modifiers = 0;
  if (env->GetClassModifiers(receiver_type, &modifiers)) { Failed(jni); return; }
  if (!(modifiers & evolution::kAccInterface)) return;
  // Public reflection encodes both pointer and index IDs. Do not decode a JNI
  // reference or read an ArtMethod while the JVMTI callback is Native.
  jobject reflected = jni->CallStaticObjectMethod(handles_class, reflect_as, method_class, delegate);
  if (jni->ExceptionCheck()) return;
  jmethodID member = reflected ? jni->FromReflectedMethod(reflected) : nullptr;
  jboolean obsolete = false;
  jclass declaring = nullptr;
  if (!member || env->IsMethodObsolete(member, &obsolete) ||
      env->GetMethodDeclaringClass(member, &declaring) || !declaring ||
      env->GetClassModifiers(declaring, &modifiers)) { Failed(jni); return; }
  if (obsolete || !(modifiers & evolution::kAccInterface)) return;
  jni->ThrowNew(incompatible_class, "MethodHandle receiver lost the declaring interface");
}
}  // namespace

void JNICALL Breakpoint(jvmtiEnv* env, JNIEnv* jni, jthread thread,
                        jmethodID method, jlocation location) {
  if (method != transform.load(std::memory_order_acquire) || location != 0) return;
  if ((!probing && !enabled.load(std::memory_order_acquire)) || checking || jni->ExceptionCheck()) return;
  if (jni->PushLocalFrame(16)) return;
  Locals locals{jni};
  struct Checking { Checking() { checking = true; } ~Checking() { checking = false; } } scope;
  Check(env, jni, thread);
  if (probing && !jni->ExceptionCheck()) observed = true;
}
void Enable() { enabled.store(true, std::memory_order_release); }
bool Prepare(JNIEnv* jni, jvmtiEnv* env) {
  if (ready.load(std::memory_order_acquire)) return true;
  bool empty = false;
  if (!preparing.compare_exchange_strong(empty, true, std::memory_order_acq_rel)) return false;
  struct Preparing { ~Preparing() { preparing.store(false, std::memory_order_release); } } scope;
  if (!jni || !env || jni->PushLocalFrame(16)) return false;
  Locals locals{jni};
  jvmtiCapabilities capabilities{};
  capabilities.can_generate_breakpoint_events = 1;
  capabilities.can_access_local_variables = 1;
  if (env->AddCapabilities(&capabilities)) return false;
  jmethodID entry = transform.load(std::memory_order_acquire);
  if (!entry) {
    // Publish immutable bootstrap metadata before enabling callbacks. A failed
    // breakpoint rollback may leave ART forwarding events until a retry.
    jclass bound = jni->FindClass("java/lang/invoke/Transformers$BindTo");
    if (!bound) return false;
    jclass handle = jni->FindClass("java/lang/invoke/MethodHandle");
    if (!handle) return false;
    jclass type = jni->FindClass("java/lang/invoke/MethodType");
    if (!type) return false;
    if (!handles_class) handles_class = Global(jni, "java/lang/invoke/MethodHandles");
    if (!handles_class) return false;
    if (!method_class) method_class = Global(jni, "java/lang/reflect/Method");
    if (!method_class) return false;
    if (!incompatible_class) incompatible_class = Global(jni, "java/lang/IncompatibleClassChangeError");
    if (!incompatible_class) return false;
    if (!internal_class) internal_class = Global(jni, "java/lang/InternalError");
    if (!internal_class) return false;
    delegate_field = Field(env, bound, "delegate", "Ljava/lang/invoke/MethodHandle;");
    receiver_field = Field(env, bound, "receiver", "Ljava/lang/Object;");
    kind_field = Field(env, handle, "handleKind", "I");
    type_field = Field(env, handle, "type", "Ljava/lang/invoke/MethodType;");
    parameters_field = Field(env, type, "ptypes", "[Ljava/lang/Class;");
    reflect_as = jni->GetStaticMethodID(handles_class, "reflectAs", "(Ljava/lang/Class;Ljava/lang/invoke/MethodHandle;)Ljava/lang/reflect/Member;");
    entry = Transform(env, bound);
    if (jni->ExceptionCheck() || !delegate_field || !receiver_field || !kind_field ||
        !type_field || !parameters_field || !reflect_as || !entry) return false;
    jlocation start = -1, end = -1;
    if (env->GetMethodLocation(entry, &start, &end) || start != 0 || end < 0) return false;
    transform.store(entry, std::memory_order_release);
  }
  if (env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr)) return false;
  const auto result = env->SetBreakpoint(entry, 0);
  if (result != JVMTI_ERROR_NONE && result != JVMTI_ERROR_DUPLICATE) {
    env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
    return false;
  }
  jclass probe = jni->FindClass("com/github/ytlog/artdcevm/internal/BoundMethodHandleProbe");
  jmethodID run = probe ? jni->GetStaticMethodID(probe, "run", "()V") : nullptr;
  observed = false;
  probing = true;
  if (run) jni->CallStaticVoidMethod(probe, run);
  probing = false;
  const bool passed = observed && !jni->ExceptionCheck();
  if (passed) ready.store(true, std::memory_order_release);
  else {
    env->ClearBreakpoint(entry, 0);
    env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
  }
  return passed;
}
}  // namespace artdcevm::bound_method_handle

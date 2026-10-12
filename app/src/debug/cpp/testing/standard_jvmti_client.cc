#include <jni.h>
#include "jvmti.h"
#include <atomic>
#include <cstdarg>
#include <vector>

// A user agent as an app would write it: its own environment, the
// can_redefine_classes capability and the standard RedefineClasses call.
namespace {
jvmtiEnv* client_env = nullptr;
jclass method_class = nullptr;
jmethodID method_ids[4]{};
std::atomic<jlong> single_step_count{0};

void JNICALL SingleStep(jvmtiEnv*, JNIEnv*, jthread, jmethodID, jlocation) {
  single_step_count.fetch_add(1, std::memory_order_relaxed);
}

jint InvokeMethodV(JNIEnv* jni, jobject receiver, jmethodID id, jint method, ...) {
  va_list arguments;
  va_start(arguments, method);
  jint value;
  if (method == 1) value = jni->CallStaticIntMethodV(method_class, id, arguments);
  else if (method == 0) value = jni->CallNonvirtualIntMethodV(receiver, method_class, id, arguments);
  else value = jni->CallIntMethodV(receiver, id, arguments);
  va_end(arguments);
  return value;
}

jint InvokeMethod(JNIEnv* jni, jobject receiver, jmethodID id, jint method, jint form) {
  if (form == 1) {
    jvalue argument{};
    argument.i = 4;
    const jvalue* arguments = method == 3 ? &argument : nullptr;
    if (method == 1) return jni->CallStaticIntMethodA(method_class, id, arguments);
    if (method == 0) return jni->CallNonvirtualIntMethodA(receiver, method_class, id, arguments);
    return jni->CallIntMethodA(receiver, id, arguments);
  }
  if (form == 2) {
    if (method == 3) return InvokeMethodV(jni, receiver, id, method, 4);
    return InvokeMethodV(jni, receiver, id, method);
  }
  // C++ JNIEnv varargs wrappers route straight to V. Use the table entry here
  // so this form actually covers the ordinary varargs entry point as well.
  if (method == 1) return jni->functions->CallStaticIntMethod(jni, method_class, id);
  if (method == 3) return jni->functions->CallIntMethod(jni, receiver, id, 4);
  if (method == 0) return jni->functions->CallNonvirtualIntMethod(jni, receiver, method_class, id);
  return jni->functions->CallIntMethod(jni, receiver, id);
}

jint InvokeCached(JNIEnv* jni, jobject receiver, jint method, jint form) {
  if (method < 0 || method >= 4 || !method_class || !method_ids[method]) {
    jclass error = jni->FindClass("java/lang/IllegalStateException");
    if (error) jni->ThrowNew(error, "Method IDs were not cached");
    return 0;
  }
  return InvokeMethod(jni, receiver, method_ids[method], method, form);
}

jint InvokeReflected(JNIEnv* jni, jobject reflected, jobject receiver, jint method, jint form) {
  jmethodID id = jni->FromReflectedMethod(reflected);
  if (jni->ExceptionCheck()) return 0;
  return InvokeMethod(jni, receiver, id, method, form);
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  if (client_env) return JNI_OK;
  if (vm->GetEnv(reinterpret_cast<void**>(&client_env), JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = 1;
  requested.can_generate_single_step_events = 1;
  if (client_env->AddCapabilities(&requested) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmtiEventCallbacks callbacks{};
  callbacks.SingleStep = SingleStep;
  return client_env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE
      ? JNI_OK : JNI_ERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_setThreadSingleStep(
    JNIEnv* jni, jclass, jboolean enabled) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  jthread current = nullptr;
  jvmtiError error = client_env->GetCurrentThread(&current);
  if (error != JVMTI_ERROR_NONE) return error;
  // The explicit current jthread selects ART's per-thread interpreter mode.
  // A null thread would select global event mode and would not test this path.
  error = client_env->SetEventNotificationMode(enabled ? JVMTI_ENABLE : JVMTI_DISABLE,
                                               JVMTI_EVENT_SINGLE_STEP, current);
  jni->DeleteLocalRef(current);
  return error;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_singleStepCount(JNIEnv*, jclass) {
  return single_step_count.load(std::memory_order_relaxed);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_redefine(
    JNIEnv* jni, jclass, jobjectArray classes, jobjectArray dexes) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!classes || !dexes) return JVMTI_ERROR_NULL_POINTER;
  const jint count = jni->GetArrayLength(classes);
  if (count != jni->GetArrayLength(dexes)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  std::vector<jvmtiClassDefinition> batch;
  std::vector<std::vector<unsigned char>> buffers(count);
  for (jint i = 0; i < count; ++i) {
    auto klass = static_cast<jclass>(jni->GetObjectArrayElement(classes, i));
    auto bytes = static_cast<jbyteArray>(jni->GetObjectArrayElement(dexes, i));
    if (!klass || !bytes) return JVMTI_ERROR_NULL_POINTER;
    const jint size = jni->GetArrayLength(bytes);
    buffers[i].resize(size);
    jni->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(buffers[i].data()));
    jni->DeleteLocalRef(bytes);
    batch.push_back({klass, size, buffers[i].data()});
  }
  return client_env->RedefineClasses(count, batch.data());
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheMethodIds(
    JNIEnv* jni, jclass, jclass type) {
  if (method_class) jni->DeleteGlobalRef(method_class);
  method_class = static_cast<jclass>(jni->NewGlobalRef(type));
  method_ids[0] = jni->GetMethodID(type, "directValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[1] = jni->GetStaticMethodID(type, "staticValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[2] = jni->GetMethodID(type, "virtualValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[3] = jni->GetMethodID(type, "signatureValue", "(I)I");
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callCachedMethod(
    JNIEnv* jni, jclass, jobject receiver, jint method) {
  return InvokeCached(jni, receiver, method, 0);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callReflectedMethod(
    JNIEnv* jni, jclass, jobject reflected, jobject receiver, jint method) {
  return InvokeReflected(jni, reflected, receiver, method, 0);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callCachedMethodVariant(
    JNIEnv* jni, jclass, jobject receiver, jint method, jint form) {
  return InvokeCached(jni, receiver, method, form);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callReflectedMethodVariant(
    JNIEnv* jni, jclass, jobject reflected, jobject receiver, jint method, jint form) {
  return InvokeReflected(jni, reflected, receiver, method, form);
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_lookupMethodId(
    JNIEnv* jni, jclass, jclass type, jint method) {
  const char* names[] = {"directValue", "staticValue", "virtualValue", "signatureValue"};
  if (method == 1) {
    (void)jni->GetStaticMethodID(type, names[method], "()I");
  } else {
    (void)jni->GetMethodID(type, names[method], method == 3 ? "(I)I" : "()I");
  }
}

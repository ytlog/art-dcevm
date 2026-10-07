#include <jni.h>
#include "jvmti.h"
#include <atomic>
#include <cstring>
#include <vector>

namespace {
jvmtiEnv* client_env = nullptr;
#if !defined(ARTDCEVM_RESTRICTED_CLIENT) && !defined(ARTDCEVM_SECOND_CLIENT)
jclass transform_target = nullptr;
std::vector<unsigned char> transform_bytes;
std::atomic<int> transform_calls{0};
void JNICALL Transform(jvmtiEnv* env, JNIEnv* jni, jclass target, jobject, const char*,
                       jobject, jint, const unsigned char*, jint* output_size,
                       unsigned char** output) {
  if (!target || !transform_target || !jni->IsSameObject(target, transform_target)) return;
  ++transform_calls;
  unsigned char* data = nullptr;
  if (env->Allocate(static_cast<jlong>(transform_bytes.size()), &data) != JVMTI_ERROR_NONE) return;
  std::memcpy(data, transform_bytes.data(), transform_bytes.size());
  *output_size = static_cast<jint>(transform_bytes.size());
  *output = data;
}
#endif
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  if (vm->GetEnv(reinterpret_cast<void**>(&client_env), JVMTI_VERSION_1_2) != JNI_OK)
    return JNI_ERR;
#ifdef ARTDCEVM_RESTRICTED_CLIENT
  return JNI_OK;
#else
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = 1;
#ifndef ARTDCEVM_SECOND_CLIENT
  jvmtiCapabilities potential{};
  if (client_env->GetPotentialCapabilities(&potential) != JVMTI_ERROR_NONE) return JNI_ERR;
  requested.can_generate_all_class_hook_events = potential.can_generate_all_class_hook_events;
  requested.can_retransform_classes = potential.can_retransform_classes;
#endif
  return client_env->AddCapabilities(&requested) == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
#endif
}
#ifdef ARTDCEVM_RESTRICTED_CLIENT
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiInstrumentation_redefineRestricted(
#elif defined(ARTDCEVM_SECOND_CLIENT)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiInstrumentation_redefinePeer(
#else
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_redefineNative(
#endif
    JNIEnv* jni, jclass, jobjectArray classes, jobjectArray dexes) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!classes || !dexes) return JVMTI_ERROR_NULL_POINTER;
  jint count = jni->GetArrayLength(classes);
  if (count != jni->GetArrayLength(dexes)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  std::vector<jvmtiClassDefinition> batch;
  std::vector<jclass> refs;
  std::vector<std::vector<unsigned char>> buffers;
  batch.reserve(count);
  refs.reserve(count);
  buffers.reserve(count);
  for (jint i = 0; i < count; ++i) {
    auto klass = static_cast<jclass>(jni->GetObjectArrayElement(classes, i));
    auto bytes = static_cast<jbyteArray>(jni->GetObjectArrayElement(dexes, i));
    refs.push_back(klass);
    if (!klass || !bytes) return JVMTI_ERROR_NULL_POINTER;
    jint size = jni->GetArrayLength(bytes);
    buffers.emplace_back(size);
    jni->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(buffers.back().data()));
    jni->DeleteLocalRef(bytes);
    batch.push_back({klass, size, buffers.back().data()});
  }
  jint result = client_env->RedefineClasses(count, batch.data());
  for (jclass klass : refs) jni->DeleteLocalRef(klass);
  return result;
}
#if !defined(ARTDCEVM_RESTRICTED_CLIENT) && !defined(ARTDCEVM_SECOND_CLIENT)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiInstrumentation_enableTransform(
    JNIEnv* jni, jclass, jclass target, jbyteArray dex) {
  if (!client_env || !target || !dex) return JVMTI_ERROR_NULL_POINTER;
  if (transform_target) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  jint size = jni->GetArrayLength(dex);
  if (size <= 0) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  transform_bytes.resize(size);
  jni->GetByteArrayRegion(dex, 0, size, reinterpret_cast<jbyte*>(transform_bytes.data()));
  transform_target = static_cast<jclass>(jni->NewGlobalRef(target));
  if (!transform_target) return JVMTI_ERROR_OUT_OF_MEMORY;
  transform_calls = 0;
  jvmtiEventCallbacks callbacks{};
  callbacks.ClassFileLoadHook = Transform;
  jvmtiError result = client_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  if (result == JVMTI_ERROR_NONE)
    result = client_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                                                  nullptr);
  if (result != JVMTI_ERROR_NONE) {
    jni->DeleteGlobalRef(transform_target);
    transform_target = nullptr;
    transform_bytes.clear();
  }
  return result;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiInstrumentation_disableTransform(JNIEnv* jni, jclass) {
  client_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
  if (transform_target) jni->DeleteGlobalRef(transform_target);
  transform_target = nullptr;
  transform_bytes.clear();
  return transform_calls.load();
}
#endif

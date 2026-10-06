#include <jni.h>
#include "jvmti.h"
#include <vector>

namespace {
jvmtiEnv* client_env = nullptr;
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
  return client_env->AddCapabilities(&requested) == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
#endif
}
#ifdef ARTDCEVM_RESTRICTED_CLIENT
extern "C" JNIEXPORT jint JNICALL
Java_dev_artdcevm_testing_StandardJvmtiInstrumentation_redefineRestricted(
#else
extern "C" JNIEXPORT jint JNICALL
Java_dev_artdcevm_testing_StandardJvmtiInstrumentation_redefine(
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

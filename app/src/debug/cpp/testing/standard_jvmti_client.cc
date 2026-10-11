#include <jni.h>
#include "jvmti.h"
#include <vector>

// A user agent as an app would write it: its own environment, the
// can_redefine_classes capability and the standard RedefineClasses call.
namespace {
jvmtiEnv* client_env = nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  if (client_env) return JNI_OK;
  if (vm->GetEnv(reinterpret_cast<void**>(&client_env), JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = 1;
  return client_env->AddCapabilities(&requested) == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
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

#include <jni.h>
#include <jvmti.h>
#include <mutex>
#include <string>
#include "platform/common/structural_redefine_engine.h"

// JVMTI agent entry and the JNI methods of com.github.ytlog.artdcevm.api.DcevmRuntime.
namespace {
std::mutex agent_mutex;
JavaVM* agent_vm = nullptr;
jvmtiEnv* agent_env = nullptr;
}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }

// Debug.attachJvmtiAgent loads this library a second time as an agent.
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  std::lock_guard<std::mutex> lock(agent_mutex);
  if (agent_env) return JNI_OK;
  jvmtiEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION_1_2) != JNI_OK || !env) return JNI_ERR;
  agent_vm = vm;
  agent_env = env;
  return JNI_OK;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeInitialize(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lock(agent_mutex);
  if (!agent_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  return artdcevm::structural_redefine::Install(agent_vm, agent_env);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeCapabilities(JNIEnv* jni, jclass) {
  return jni->NewStringUTF(artdcevm::structural_redefine::Capabilities().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeErrorName(JNIEnv* jni, jclass, jint code) {
  std::string name = "JVMTI_ERROR_" + std::to_string(code);
  std::lock_guard<std::mutex> lock(agent_mutex);
  char* text = nullptr;
  if (agent_env && agent_env->GetErrorName(static_cast<jvmtiError>(code), &text) == JVMTI_ERROR_NONE) {
    name = text;
    agent_env->Deallocate(reinterpret_cast<unsigned char*>(text));
  }
  return jni->NewStringUTF(name.c_str());
}

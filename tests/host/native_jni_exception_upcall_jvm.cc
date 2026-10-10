#include "platform/common/native_jni_exception_upcall.h"
using artdcevm::jni_exception_upcall::Probe;
namespace {
thread_local Probe probe;
jlong JNICALL Prepare(JNIEnv* env, jclass klass, jthrowable expected) { return probe.Prepare(env, klass, expected); }
void JNICALL Invoke(JNIEnv* env, jclass, jlong request) { probe.Invoke(env, request); }
jboolean JNICALL Complete(JNIEnv* env, jclass, jlong request, jthrowable caught) {
  const bool success = probe.Complete(env, request, caught);
  // A receipt cannot be replayed even with the same expected exception.
  return success && !probe.Complete(env, request, caught) ? JNI_TRUE : JNI_FALSE;
}
void JNICALL Cancel(JNIEnv* env, jclass, jlong request) { probe.Cancel(env, request); }
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  auto klass = env->FindClass("com/github/ytlog/artdcevm/internal/JniExceptionUpcallFixture");
  if (!klass) return JNI_ERR;
  JNINativeMethod methods[] = {
    {const_cast<char*>("prepare"), const_cast<char*>("(Ljava/lang/Throwable;)J"), reinterpret_cast<void*>(Prepare)},
    {const_cast<char*>("invoke"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(Invoke)},
    {const_cast<char*>("complete"), const_cast<char*>("(JLjava/lang/Throwable;)Z"), reinterpret_cast<void*>(Complete)},
    {const_cast<char*>("cancel"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(Cancel)},
  };
  const auto result = env->RegisterNatives(klass, methods, 4);
  env->DeleteLocalRef(klass);
  return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}

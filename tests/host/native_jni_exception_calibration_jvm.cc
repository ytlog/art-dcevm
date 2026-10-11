#include "platform/common/native_jni_exception_calibration.h"
#include "platform/common/native_schema_run_state.h"
#include <cassert>
#include <string>
namespace {
thread_local bool schema_window = false;
artdcevm::native_jni_exception_calibration::Service service;
jboolean JNICALL Calibrate(JNIEnv* env, jclass) {
  std::string error;
  if (!service.Calibrate(env, &error)) return JNI_FALSE;
  JavaVM* vm = nullptr;
  assert(env->GetJavaVM(&vm) == JNI_OK);
  assert(service.Calibrated() && service.MatchesVm(vm));
  assert(!service.MatchesVm(nullptr));
  assert(!service.MatchesVm(reinterpret_cast<JavaVM*>(uintptr_t(1))));
  return JNI_TRUE;
}
jboolean JNICALL Blocked(JNIEnv* env, jclass) {
  assert(!service.Calibrated()); schema_window = true;
  std::string error; const bool rejected = !service.Calibrate(env, &error);
  schema_window = false;
  return rejected && !service.Calibrated() && !env->ExceptionCheck() ? JNI_TRUE : JNI_FALSE;
}
}
namespace artdcevm::native_schema_run_state {
RunState* RunState::Current() noexcept {
  return schema_window ? reinterpret_cast<RunState*>(uintptr_t(1)) : nullptr;
}
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  auto klass = env->FindClass("JniExceptionCalibrationHost");
  if (!klass || env->ExceptionCheck()) return JNI_ERR;
  JNINativeMethod methods[] = {
    {const_cast<char*>("calibrate"), const_cast<char*>("()Z"), reinterpret_cast<void*>(Calibrate)},
    {const_cast<char*>("blockedWindow"), const_cast<char*>("()Z"), reinterpret_cast<void*>(Blocked)},
  };
  const auto result = env->RegisterNatives(klass, methods, 2);
  env->DeleteLocalRef(klass);
  return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}

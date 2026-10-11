#include "platform/common/native_jni_exception_calibration.h"
#include "platform/common/native_jni_exception_upcall.h"
#include "platform/common/native_schema_run_state.h"

namespace artdcevm::native_jni_exception_calibration {
namespace {
constexpr char kFixture[] = "com/github/ytlog/artdcevm/internal/JniExceptionUpcallFixture";
bool Fail(std::string* error, const char* message) { if (error) *error = message; return false; }
struct Invocation {
  JNIEnv* env; jclass fixture;
  jlong request = 0;
  unsigned prepares = 0, invokes = 0, completions = 0;
  bool pending_returned = false, matched = false;
};
thread_local Invocation* current = nullptr;
thread_local jni_exception_upcall::Probe probe;
bool Owns(JNIEnv* env, jclass fixture) {
  return current && current->env == env && !native_schema_run_state::RunState::Current() &&
    !env->ExceptionCheck() && env->IsSameObject(current->fixture, fixture) && !env->ExceptionCheck();
}
jlong JNICALL Prepare(JNIEnv* env, jclass fixture, jthrowable token) {
  if (!Owns(env, fixture) || current->prepares++) return 0;
  current->request = probe.Prepare(env, fixture, token);
  return current->request;
}
void JNICALL Invoke(JNIEnv* env, jclass fixture, jlong request) {
  if (!Owns(env, fixture) || !request || request != current->request || current->invokes++) return;
  current->pending_returned = probe.Invoke(env, request);
}
jboolean JNICALL Complete(JNIEnv* env, jclass fixture, jlong request, jthrowable caught) {
  if (!Owns(env, fixture) || !request || request != current->request || current->completions++) return JNI_FALSE;
  current->matched = probe.Complete(env, request, caught);
  return current->matched ? JNI_TRUE : JNI_FALSE;
}
void JNICALL Cancel(JNIEnv* env, jclass fixture, jlong request) {
  if (!Owns(env, fixture) || !request || request != current->request) return;
  probe.Cancel(env, request);
}
struct Local {
  JNIEnv* env; jobject value;
  ~Local() { if (value) env->DeleteLocalRef(value); }
};
JNINativeMethod methods[] = {
  {const_cast<char*>("prepare"), const_cast<char*>("(Ljava/lang/Throwable;)J"), reinterpret_cast<void*>(Prepare)},
  {const_cast<char*>("invoke"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(Invoke)},
  {const_cast<char*>("complete"), const_cast<char*>("(JLjava/lang/Throwable;)Z"), reinterpret_cast<void*>(Complete)},
  {const_cast<char*>("cancel"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(Cancel)},
};
}
bool Service::CleanupLocked(JNIEnv* env, std::string* error) {
  if (!env || env->ExceptionCheck() || current || native_schema_run_state::RunState::Current())
    return Fail(error, "JNI upcall cleanup requires ordinary exception-free owner");
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || !vm || (vm_.load(std::memory_order_relaxed) && vm_.load(std::memory_order_relaxed) != vm))
    return Fail(error, "JNI upcall cleanup VM differs");
  if (registered_) {
    if (env->UnregisterNatives(fixture_) != JNI_OK || env->ExceptionCheck())
      return Fail(error, "JNI upcall owned fixture unregister failed");
    registered_ = false;
  }
  if (fixture_) { env->DeleteGlobalRef(fixture_); fixture_ = nullptr; }
  return true;
}
bool Service::Cleanup(JNIEnv* env, std::string* error) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return Fail(error, "JNI upcall owner is busy");
  return CleanupLocked(env, error);
}
bool Service::Calibrate(JNIEnv* env, std::string* error) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return Fail(error, "JNI upcall owner is busy");
  if (!env || env->ExceptionCheck() || current || native_schema_run_state::RunState::Current())
    return Fail(error, "JNI upcall calibration requires ordinary exception-free owner outside schema Run");
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || !vm) return Fail(error, "JNI upcall VM unavailable");
  if (Calibrated()) return MatchesVm(vm) || Fail(error, "JNI upcall calibrated VM differs");
  if (vm_.load(std::memory_order_relaxed) && vm_.load(std::memory_order_relaxed) != vm)
    return Fail(error, "JNI upcall owner VM differs");
  vm_.store(vm, std::memory_order_relaxed);
  if (!CleanupLocked(env, error)) return false;
  Local klass{env, env->FindClass(kFixture)};
  if (!klass.value || env->ExceptionCheck()) return Fail(error, "JNI upcall owned fixture missing");
  fixture_ = static_cast<jclass>(env->NewGlobalRef(klass.value));
  if (!fixture_ || env->ExceptionCheck()) return Fail(error, "JNI upcall owned fixture root failed");
  auto run = env->GetStaticMethodID(fixture_, "run", "(Ljava/lang/Throwable;)Z");
  if (!run || env->ExceptionCheck()) return Fail(error, "JNI upcall owned Java catch entry missing");
  Local throwable{env, env->FindClass("java/lang/IllegalStateException")};
  if (!throwable.value || env->ExceptionCheck()) return Fail(error, "JNI upcall Throwable class missing");
  auto constructor = env->GetMethodID(static_cast<jclass>(throwable.value), "<init>", "()V");
  if (!constructor || env->ExceptionCheck()) return Fail(error, "JNI upcall Throwable constructor missing");
  Local token{env, env->NewObjectA(static_cast<jclass>(throwable.value), constructor, nullptr)};
  if (!token.value || env->ExceptionCheck()) return Fail(error, "JNI upcall real Throwable creation failed");
  const auto registration = env->RegisterNatives(fixture_, methods, 4);
  registered_ = registration == JNI_OK;
  if (!registered_ || env->ExceptionCheck())
    return Fail(error, "JNI upcall owned native registration failed");
  Invocation invocation{env, fixture_}; current = &invocation;
  jvalue argument{}; argument.l = token.value;
  const auto result = env->CallStaticBooleanMethodA(fixture_, run, &argument);
  // Release any incomplete Probe roots without clearing or changing pending.
  if (invocation.request) probe.Cancel(env, invocation.request);
  current = nullptr;
  if (env->ExceptionCheck()) return Fail(error, "JNI upcall original Java calibration failed with pending exception");
  const bool receipt = result == JNI_TRUE && invocation.request && invocation.prepares == 1 &&
    invocation.invokes == 1 && invocation.completions == 1 && invocation.pending_returned && invocation.matched &&
    !native_schema_run_state::RunState::Current();
  if (!CleanupLocked(env, error)) return false;
  if (!receipt) return Fail(error, "JNI upcall complete original catch/identity receipt missing");
  ready_.store(true, std::memory_order_release);
  return true;
}
}

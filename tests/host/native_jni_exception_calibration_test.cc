#include "platform/common/native_jni_exception_calibration.h"
#include "platform/common/native_schema_run_state.h"
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
int vm, fixture, throwable_class, token, wrong, method;
int globals = 0, registration = 0, unregisters = 0, mode = 0;
bool window = false;
jthrowable pending = nullptr;
const JNINativeMethod* entries = nullptr;
template<class T> T Ref(void* p) { return reinterpret_cast<T>(p); }
jboolean JNICALL Check(JNIEnv*) { return pending != nullptr; }
jthrowable JNICALL Occurred(JNIEnv*) { return pending; }
void JNICALL DeleteLocal(JNIEnv*, jobject) {}
jint JNICALL VM(JNIEnv*, JavaVM** result) { assert(!pending); *result = Ref<JavaVM*>(&vm); return JNI_OK; }
jclass JNICALL Find(JNIEnv*, const char* name) { assert(!pending); return Ref<jclass>(std::strstr(name, "JniException") ? &fixture : &throwable_class); }
jobject JNICALL Global(JNIEnv*, jobject value) { assert(!pending); ++globals; return value; }
void JNICALL DeleteGlobal(JNIEnv*, jobject) { --globals; }
jmethodID JNICALL Method(JNIEnv*, jclass, const char*, const char*) { assert(!pending); return Ref<jmethodID>(&method); }
jobject JNICALL New(JNIEnv*, jclass, jmethodID, const jvalue*) { assert(!pending); return Ref<jobject>(&token); }
jboolean JNICALL Same(JNIEnv*, jobject a, jobject b) { assert(!pending); return a == b; }
jint JNICALL Register(JNIEnv*, jclass, const JNINativeMethod* table, jint count) { assert(count == 4); entries = table; registration = 1; return JNI_OK; }
jint JNICALL Unregister(JNIEnv*, jclass) { assert(!pending); registration = 0; ++unregisters; return JNI_OK; }
void JNICALL Thrower(JNIEnv*, jclass, jmethodID, const jvalue* arg) { pending = static_cast<jthrowable>(arg->l); }
jboolean JNICALL Run(JNIEnv* env, jclass klass, jmethodID, const jvalue* arg) {
  if (mode == 1) return JNI_TRUE; // Fake Java bool without any private receipt.
  using Prepare = jlong (*)(JNIEnv*, jclass, jthrowable);
  using Invoke = void (*)(JNIEnv*, jclass, jlong);
  using Complete = jboolean (*)(JNIEnv*, jclass, jlong, jthrowable);
  auto request = reinterpret_cast<Prepare>(entries[0].fnPtr)(env, klass, static_cast<jthrowable>(arg->l));
  assert(request);
  if (mode == 2) return JNI_TRUE; // Prepared but no original invoke or Complete.
  reinterpret_cast<Invoke>(entries[1].fnPtr)(env, klass, request);
  assert(pending == Ref<jthrowable>(&token));
  if (mode == 3) return JNI_TRUE; // Escape to native with original pending.
  auto caught = pending;
  pending = nullptr; // Model Java catch; never supplied by the producer JNI table.
  if (mode == 4) return JNI_TRUE; // Java caught but missing Complete.
  if (mode == 5) caught = Ref<jthrowable>(&wrong);
  const auto result = reinterpret_cast<Complete>(entries[2].fnPtr)(env, klass, request, caught);
  if (mode == 6) reinterpret_cast<Complete>(entries[2].fnPtr)(env, klass, request, caught); // Replay poisons counts.
  return mode == 5 ? JNI_TRUE : result;
}
}
namespace artdcevm::native_schema_run_state {
RunState* RunState::Current() noexcept { return window ? Ref<RunState*>(&method) : nullptr; }
}
int main() {
  JNINativeInterface_ table{};
  table.ExceptionCheck = Check; table.ExceptionOccurred = Occurred; table.DeleteLocalRef = DeleteLocal;
  table.GetJavaVM = VM; table.FindClass = Find; table.NewGlobalRef = Global; table.DeleteGlobalRef = DeleteGlobal;
  table.GetStaticMethodID = Method; table.GetMethodID = Method; table.NewObjectA = New;
  table.IsSameObject = Same; table.RegisterNatives = Register; table.UnregisterNatives = Unregister;
  table.CallStaticVoidMethodA = Thrower; table.CallStaticBooleanMethodA = Run;
  JNIEnv env{&table}; std::string error;
  for (mode = 0; mode <= 6; ++mode) {
    artdcevm::native_jni_exception_calibration::Service owner;
    assert(!owner.Calibrated() && !owner.MatchesVm(Ref<JavaVM*>(&vm)));
    window = true; assert(!owner.Calibrate(&env, &error)); window = false;
    pending = Ref<jthrowable>(&wrong); assert(!owner.Calibrate(&env, &error) && pending == Ref<jthrowable>(&wrong)); pending = nullptr;
    const bool result = owner.Calibrate(&env, &error);
    assert(result == (mode == 0)); assert(owner.Calibrated() == result);
    if (mode == 3) {
      assert(pending == Ref<jthrowable>(&token) && registration == 1 && globals == 1);
      assert(!owner.Cleanup(&env, &error) && pending == Ref<jthrowable>(&token));
      pending = nullptr; assert(owner.Cleanup(&env, &error));
    }
    assert(globals == 0 && registration == 0 && !pending);
    assert(owner.MatchesVm(Ref<JavaVM*>(&vm)) == result && !owner.MatchesVm(Ref<JavaVM*>(&wrong)));
  }
  assert(unregisters == 7);
  std::cout << "Closed JNI Service rejects fabricated bool, incomplete/replayed receipt, wrong identity, active Run and preserves failed pending exception passed\n";
}

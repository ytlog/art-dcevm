#include "platform/common/native_jni_exception_upcall.h"
#include <cassert>
#include <iostream>
#include <thread>
using artdcevm::jni_exception_upcall::Probe;
namespace {
int vm_token, class_token, expected_token, wrong_token, method_token;
jthrowable pending = nullptr;
int invokes = 0, globals = 0, locals_deleted = 0;
bool returns_normally = false, loses_pending = false;
template<class T> T Ref(void* value) { return reinterpret_cast<T>(value); }
jboolean JNICALL Check(JNIEnv*) { return pending != nullptr; }
jthrowable JNICALL Occurred(JNIEnv*) { return pending; }
void JNICALL DeleteLocal(JNIEnv*, jobject) { ++locals_deleted; if (loses_pending) pending = nullptr; }
jint JNICALL GetVM(JNIEnv*, JavaVM** output) { assert(!pending); *output = Ref<JavaVM*>(&vm_token); return JNI_OK; }
jmethodID JNICALL Method(JNIEnv*, jclass, const char*, const char*) { assert(!pending); return Ref<jmethodID>(&method_token); }
jobject JNICALL Global(JNIEnv*, jobject value) { assert(!pending); ++globals; return value; }
void JNICALL DeleteGlobal(JNIEnv*, jobject) { --globals; }
jboolean JNICALL Same(JNIEnv*, jobject a, jobject b) { assert(!pending); return a == b; }
void JNICALL Call(JNIEnv*, jclass klass, jmethodID method, const jvalue* argument) {
  assert(!pending && klass == Ref<jclass>(&class_token) && method == Ref<jmethodID>(&method_token));
  ++invokes;
  if (!returns_normally) pending = static_cast<jthrowable>(argument->l);
}
}
int main() {
  JNINativeInterface_ table{};
  table.ExceptionCheck = Check; table.ExceptionOccurred = Occurred;
  table.DeleteLocalRef = DeleteLocal; table.GetJavaVM = GetVM;
  table.GetStaticMethodID = Method; table.NewGlobalRef = Global;
  table.DeleteGlobalRef = DeleteGlobal; table.IsSameObject = Same;
  table.CallStaticVoidMethodA = Call;
  // No ExceptionClear/Throw entry exists: accidental synthetic propagation crashes.
  JNIEnv env{&table}, foreign{&table}; Probe probe;
  auto klass = Ref<jclass>(&class_token); auto token = Ref<jthrowable>(&expected_token);
  auto wrong = Ref<jthrowable>(&wrong_token);
  pending = wrong; assert(!probe.Prepare(&env, klass, token)); assert(pending == wrong); pending = nullptr;
  auto request = probe.Prepare(&env, klass, token); assert(request && globals == 2);
  assert(!probe.Prepare(&env, klass, token));
  assert(!probe.Invoke(&foreign, request)); assert(!probe.Invoke(&env, request + 1));
  std::thread other([&] { assert(!probe.Invoke(&env, request)); assert(!probe.Cancel(&env, request)); }); other.join();
  assert(probe.Invoke(&env, request) && pending == token && invokes == 1 && locals_deleted == 1);
  assert(!probe.Invoke(&env, request) && pending == token);
  assert(!probe.Complete(&env, request, token) && pending == token && globals == 2);
  // Independent Java catch normally consumes pending; producer has never cleared it.
  pending = nullptr; assert(probe.Complete(&env, request, token));
  assert(globals == 0 && !probe.Complete(&env, request, token));
  auto second = probe.Prepare(&env, klass, token); assert(second > request);
  assert(!probe.Complete(&env, second, token) && globals == 0); // no invocation
  auto third = probe.Prepare(&env, klass, token); assert(probe.Invoke(&env, third));
  pending = nullptr; assert(!probe.Complete(&env, third, wrong) && globals == 0);
  assert(!probe.Complete(&env, third, token)); // bad witness also consumed
  returns_normally = true;
  auto fourth = probe.Prepare(&env, klass, token); assert(!probe.Invoke(&env, fourth));
  assert(!probe.Complete(&env, fourth, token) && globals == 0);
  returns_normally = false; loses_pending = true;
  auto fifth = probe.Prepare(&env, klass, token); assert(!probe.Invoke(&env, fifth));
  assert(!probe.Complete(&env, fifth, token) && globals == 0);
  loses_pending = false;
  auto sixth = probe.Prepare(&env, klass, token); assert(probe.Invoke(&env, sixth));
  assert(probe.Cancel(&env, sixth) && pending == token && globals == 0); pending = nullptr;
  std::cout << "JNI upcall adversarial ownership, no-clear propagation, lost-pending, identity and one-shot tests passed\n";
}

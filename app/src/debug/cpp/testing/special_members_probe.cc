#include <jni.h>
#include <cstdarg>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
namespace {
std::atomic<bool> block_kept{false};
std::mutex native_mutex;
std::condition_variable native_condition;
bool native_entered = false, native_released = false;
jobject retained = nullptr;
jclass retained_class = nullptr;
jmethodID ctor = nullptr, gone = nullptr, statik = nullptr, kept = nullptr;
void Throw(JNIEnv* env, const char* message) {
  env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), message);
}
jint NativeGone(JNIEnv* env, jobject receiver, jint x) {
  auto cls = env->GetObjectClass(receiver);
  auto field = env->GetFieldID(cls, "value", "I");
  env->DeleteLocalRef(cls);
  if (!field || env->ExceptionCheck()) return 0;
  if (x == -1) {
    std::unique_lock<std::mutex> lock(native_mutex);
    native_entered = true; native_condition.notify_all();
    if (!native_condition.wait_for(lock, std::chrono::seconds(20), [] { return native_released; })) {
      lock.unlock(); Throw(env, "Timed out waiting to release native frame"); return 0;
    }
    x = 900;
  }
  return env->GetIntField(receiver, field) + x + 100;
}
jlong NativeStatic(JNIEnv*, jclass, jlong x) { return x + 2000; }
jint NativeKept(JNIEnv* env, jobject receiver) {
  if (block_kept.load()) {
    auto control = env->FindClass("dev/artdcevm/fixture/DeletionControl");
    if (!control) return 0;
    auto await = env->GetStaticMethodID(control, "awaitRelease", "()V");
    if (await) env->CallStaticVoidMethod(control, await);
    env->DeleteLocalRef(control);
    if (env->ExceptionCheck()) return 0;
  }
  auto cls = env->GetObjectClass(receiver);
  auto field = env->GetFieldID(cls, "value", "I");
  env->DeleteLocalRef(cls);
  return field && !env->ExceptionCheck() ? env->GetIntField(receiver, field) + 300 : 0;
}
jobject NewVarargs(JNIEnv* env, jclass cls, jmethodID method, ...) {
  va_list args; va_start(args, method);
  auto out = env->NewObjectV(cls, method, args);
  va_end(args); return out;
}
jint CallVarargs(JNIEnv* env, jobject object, jmethodID method, ...) {
  va_list args; va_start(args, method);
  jint out = env->CallIntMethodV(object, method, args);
  va_end(args); return out;
}
jlong StaticVarargs(JNIEnv* env, jclass cls, jmethodID method, ...) {
  va_list args; va_start(args, method);
  jlong out = env->CallStaticLongMethodV(cls, method, args);
  va_end(args); return out;
}
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_registerSpecialNatives(
    JNIEnv* env, jclass, jclass cls) {
  const JNINativeMethod methods[] = {
    {"nativeGone", "(I)I", reinterpret_cast<void*>(NativeGone)},
    {"nativeStatic", "(J)J", reinterpret_cast<void*>(NativeStatic)},
    {"nativeKept", "()I", reinterpret_cast<void*>(NativeKept)},
  };
  if (env->RegisterNatives(cls, methods, 3) != JNI_OK && !env->ExceptionCheck()) Throw(env, "Native registration failed");
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_retainSpecialMembers(
    JNIEnv* env, jclass, jobject object) {
  if (retained) env->DeleteGlobalRef(retained);
  if (retained_class) env->DeleteGlobalRef(retained_class);
  auto cls = env->GetObjectClass(object);
  retained = env->NewGlobalRef(object);
  retained_class = static_cast<jclass>(env->NewGlobalRef(cls));
  ctor = env->GetMethodID(cls, "<init>", "(I)V");
  gone = env->GetMethodID(cls, "nativeGone", "(I)I");
  statik = env->GetStaticMethodID(cls, "nativeStatic", "(J)J");
  kept = env->GetMethodID(cls, "nativeKept", "()I");
  env->DeleteLocalRef(cls);
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_registerKeptNative(
    JNIEnv* env, jclass, jclass cls) {
  const JNINativeMethod method{"nativeKept", "()I", reinterpret_cast<void*>(NativeKept)};
  if (env->RegisterNatives(cls, &method, 1) != JNI_OK && !env->ExceptionCheck())
    Throw(env, "Kept native registration failed");
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_blockKeptNative(
    JNIEnv*, jclass, jboolean block) { block_kept.store(block); }
extern "C" JNIEXPORT jobject JNICALL Java_dev_artdcevm_testing_NativeTestBridge_createSpecial(
    JNIEnv* env, jclass, jint mode, jint x) {
  if (!retained_class || !ctor) { Throw(env, "No cached constructor"); return nullptr; }
  jvalue args[1]{}; args[0].i = x;
  switch (mode) {
    case 0: return env->NewObject(retained_class, ctor, x);
    case 1: return env->NewObjectA(retained_class, ctor, args);
    case 2: return NewVarargs(env, retained_class, ctor, x);
    case 3: {
      auto object = env->AllocObject(retained_class);
      if (!object) return nullptr;
      env->CallNonvirtualVoidMethodA(object, retained_class, ctor, args);
      if (env->ExceptionCheck()) { env->DeleteLocalRef(object); return nullptr; }
      return object;
    }
    default: Throw(env, "Unknown constructor mode"); return nullptr;
  }
}
extern "C" JNIEXPORT jlong JNICALL Java_dev_artdcevm_testing_NativeTestBridge_invokeSpecialNative(
    JNIEnv* env, jclass, jint mode) {
  if (!retained || !gone || !statik || !kept) { Throw(env, "No cached native methods"); return 0; }
  jvalue instance_args[1]{}; instance_args[0].i = 4;
  jvalue static_args[1]{}; static_args[0].j = 6;
  switch (mode) {
    case 0: return env->CallIntMethod(retained, gone, 4);
    case 1: return env->CallIntMethodA(retained, gone, instance_args);
    case 2: return CallVarargs(env, retained, gone, 4);
    case 3: return env->CallNonvirtualIntMethodA(retained, retained_class, gone, instance_args);
    case 4: return env->CallStaticLongMethod(retained_class, statik, jlong{6});
    case 5: return env->CallStaticLongMethodA(retained_class, statik, static_args);
    case 6: return StaticVarargs(env, retained_class, statik, jlong{6});
    case 7: return env->CallIntMethod(retained, kept);
    case 8: return env->CallIntMethodA(retained, kept, nullptr);
    case 9: return CallVarargs(env, retained, kept);
    case 10: return env->CallNonvirtualIntMethodA(retained, retained_class, kept, nullptr);
    default: Throw(env, "Unknown native mode"); return 0;
  }
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_artdcevm_testing_NativeTestBridge_waitSpecialNative(
    JNIEnv*, jclass) {
  std::unique_lock<std::mutex> lock(native_mutex);
  return native_condition.wait_for(lock, std::chrono::seconds(10), [] { return native_entered; });
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_releaseSpecialNative(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lock(native_mutex);
  native_released = true; native_condition.notify_all();
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_releaseSpecialMembers(
    JNIEnv* env, jclass) {
  if (retained) env->DeleteGlobalRef(retained);
  if (retained_class) env->DeleteGlobalRef(retained_class);
  retained = nullptr; retained_class = nullptr; ctor = gone = statik = kept = nullptr;
}
extern "C" JNIEXPORT jobject JNICALL Java_dev_artdcevm_testing_NativeTestBridge_createChangedSpecial(
    JNIEnv* env, jclass, jint mode, jlong x) {
  if (!retained_class) { Throw(env, "No cached class"); return nullptr; }
  auto method = env->GetMethodID(retained_class, "<init>", "(J)V");
  if (!method || env->ExceptionCheck()) return nullptr;
  jvalue args[1]{}; args[0].j = x;
  switch (mode) {
    case 0: return env->NewObject(retained_class, method, x);
    case 1: return env->NewObjectA(retained_class, method, args);
    case 2: return NewVarargs(env, retained_class, method, x);
    case 3: {
      auto object = env->AllocObject(retained_class);
      if (!object) return nullptr;
      env->CallNonvirtualVoidMethodA(object, retained_class, method, args);
      if (env->ExceptionCheck()) { env->DeleteLocalRef(object); return nullptr; }
      return object;
    }
    default: Throw(env, "Unknown constructor mode"); return nullptr;
  }
}

#pragma once
#include <jni.h>
#include <cstdint>
#include <thread>

namespace artdcevm::jni_exception_upcall {
// Ordinary JNI calibration only. This receipt proves propagation through the
// original JNI return into this owned Java catch fixture. It does NOT authorize
// boot-frame escape, ManagedStack termination, native ingress or root migration.
// The registrar must bind only JniExceptionUpcallFixture's exact native methods.
class Probe final {
 public:
  Probe() = default;
  Probe(const Probe&) = delete;
  Probe& operator=(const Probe&) = delete;
  // Caller owns this Probe on one native thread, and must Cancel before disposal
  // if the Java catch witness cannot complete. No cached raw managed pointers.
  jlong Prepare(JNIEnv* env, jclass fixture, jthrowable expected) {
    if (!env || !fixture || !expected || root_ || env->ExceptionCheck()) return 0;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || !vm) return 0;
    auto method = env->GetStaticMethodID(fixture, "throwSame", "(Ljava/lang/Throwable;)V");
    if (!method || env->ExceptionCheck() || serial_ == INT64_MAX) return 0;
    auto klass = static_cast<jclass>(env->NewGlobalRef(fixture));
    if (!klass || env->ExceptionCheck()) {
      if (klass) env->DeleteGlobalRef(klass);
      return 0;
    }
    auto root = env->NewGlobalRef(expected);
    if (!root || env->ExceptionCheck()) {
      if (root) env->DeleteGlobalRef(root);
      env->DeleteGlobalRef(klass);
      return 0;
    }
    vm_ = vm; env_ = env; thread_ = std::this_thread::get_id();
    root_ = root; klass_ = klass; method_ = method; request_ = ++serial_; phase_ = Phase::kPrepared;
    return request_;
  }
  // Returns to Java WITH the original pending Throwable. In particular, never
  // clear/rethrow it, and never call IsSameObject while an exception is pending.
  bool Invoke(JNIEnv* env, jlong request) {
    if (!Owns(env, request) || phase_ != Phase::kPrepared || env->ExceptionCheck()) return false;
    phase_ = Phase::kInvoked;
    jvalue argument{}; argument.l = root_;
    env->CallStaticVoidMethodA(klass_, method_, &argument);
    if (!env->ExceptionCheck()) return false;
    auto occurred = env->ExceptionOccurred();
    if (!occurred) return false;
    env->DeleteLocalRef(occurred);  // Allowed with pending exceptions.
    if (!env->ExceptionCheck()) return false;
    phase_ = Phase::kPendingReturned;
    return true;
  }
  // Called only by the owned Java catch body, with its actual caught object.
  // Java has consumed the exception normally; no native ExceptionClear occurs.
  // One attempted witness consumes the request, even when identity is wrong.
  bool Complete(JNIEnv* env, jlong request, jthrowable caught) {
    if (!Owns(env, request) || env->ExceptionCheck()) return false;
    bool matched = phase_ == Phase::kPendingReturned && caught && env->IsSameObject(root_, caught);
    matched = matched && !env->ExceptionCheck();
    Release(env);
    return matched;
  }
  bool Cancel(JNIEnv* env, jlong request) {
    if (!Owns(env, request)) return false;
    Release(env);  // DeleteGlobalRef is allowed even with pending exceptions.
    return true;
  }
 private:
  enum class Phase { kEmpty, kPrepared, kInvoked, kPendingReturned };
  bool Owns(JNIEnv* env, jlong request) const {
    // Check thread/env BEFORE any JNI call; foreign-thread JNIEnv is invalid.
    if (!env || env != env_ || !root_ || !request || request != request_ ||
        std::this_thread::get_id() != thread_) return false;
    // GetJavaVM is not permitted with a pending exception. The exact original
    // env/VM pair was bound before invocation and remains on this same thread.
    return vm_ != nullptr;
  }
  void Release(JNIEnv* env) {
    env->DeleteGlobalRef(root_); env->DeleteGlobalRef(klass_); klass_ = nullptr; root_ = nullptr; method_ = nullptr;
    vm_ = nullptr; env_ = nullptr; request_ = 0; phase_ = Phase::kEmpty;
  }
  JavaVM* vm_ = nullptr; JNIEnv* env_ = nullptr; jobject root_ = nullptr;
  jclass klass_ = nullptr; jmethodID method_ = nullptr; std::thread::id thread_{};
  jlong serial_ = 0, request_ = 0; Phase phase_ = Phase::kEmpty;
};
}  // namespace artdcevm::jni_exception_upcall

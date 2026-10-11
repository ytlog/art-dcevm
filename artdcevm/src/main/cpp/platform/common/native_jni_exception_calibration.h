#pragma once
#include <jni.h>
#include <atomic>
#include <mutex>
#include <string>

namespace artdcevm::native_jni_exception_calibration {
// Closed ordinary JNI calibration producer, independently verified through the
// exact owned Java catch fixture. This service does not authorize stack escape.
class Service final {
 public:
  Service() = default;
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;
  bool Calibrate(JNIEnv*, std::string*);
  // Failure preserves original pending exceptions. If registration remains,
  // call Cleanup only after the original caller normally consumes the exception.
  bool Cleanup(JNIEnv*, std::string*);
  bool Calibrated() const noexcept { return ready_.load(std::memory_order_acquire); }
  bool MatchesVm(JavaVM* vm) const noexcept {
    return vm && Calibrated() && vm_.load(std::memory_order_relaxed) == vm;
  }
 private:
  bool CleanupLocked(JNIEnv*, std::string*);
  std::mutex mutex_;
  std::atomic<bool> ready_{false};
  std::atomic<JavaVM*> vm_{nullptr};
  jclass fixture_ = nullptr;
  bool registered_ = false;
};
}

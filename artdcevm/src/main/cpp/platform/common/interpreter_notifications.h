#pragma once
#include <jvmti.h>
#include <mutex>
#include <cstdint>
namespace artdcevm::interpreter_events {
// Native setup only. Reentrant JVMTI callbacks may change requests during a
// setter: reconcile the latest generation before publishing success.
class NotificationLeases {
 public:
  jvmtiError Bound(jvmtiEnv* env, bool value) { return Request(env, bound_, value); }
  jvmtiError Watch(jvmtiEnv* env, bool value) { return Request(env, watch_, value); }
  jvmtiError Pin(jvmtiEnv* env) { return Request(env, continuation_, true); }
  void Committed() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sticky_ = true; ++generation_;
  }
  jvmtiError Release(jvmtiEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sticky_) return env == env_ ? JVMTI_ERROR_NONE : JVMTI_ERROR_INVALID_ENVIRONMENT;
    return Request(env, continuation_, false);
  }
 private:
  struct RequestState { bool enabled = false; uint64_t revision = 0; };
  jvmtiError Request(jvmtiEnv* env, RequestState& slot, bool value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!env || (env_ && env_ != env)) return JVMTI_ERROR_INVALID_ENVIRONMENT;
    env_ = env;
    const bool previous = slot.enabled; slot.enabled = value;
    const auto request = ++slot.revision; ++generation_;
    const jvmtiError error = Apply();
    if (error != JVMTI_ERROR_NONE) {
      if (slot.revision == request) { slot.enabled = previous; ++slot.revision; }
      ++generation_;
      (void)Apply();  // A failed preparation never grants admission.
    }
    return error;
  }
  jvmtiError Apply() {
    for (int attempt = 0; attempt < 8; ++attempt) {
      const bool desired[] = {bound_.enabled || continuation_.enabled || sticky_, watch_.enabled || continuation_.enabled || sticky_};
      const jvmtiEvent events[] = {JVMTI_EVENT_BREAKPOINT, JVMTI_EVENT_CLASS_PREPARE};
      bool changed = false;
      for (int i = 0; i < 2; ++i) {
        if (known_[i] && applied_[i] == desired[i]) continue;
        const auto generation = generation_;
        jvmtiError error = env_->SetEventNotificationMode(desired[i] ? JVMTI_ENABLE : JVMTI_DISABLE, events[i], nullptr);
        if (error != JVMTI_ERROR_NONE) { known_[i] = false; return error; }
        if (generation != generation_) { known_[i] = false; changed = true; break; }
        applied_[i] = desired[i]; known_[i] = true;
      }
      if (!changed) return JVMTI_ERROR_NONE;
    }
    return JVMTI_ERROR_INTERNAL;
  }
  std::recursive_mutex mutex_;
  jvmtiEnv* env_ = nullptr;
  RequestState bound_, watch_, continuation_;
  bool sticky_ = false;
  bool known_[2] = {true, true}, applied_[2] = {false, false};
  unsigned generation_ = 0;
};
jvmtiError BoundNotification(jvmtiEnv*, bool);
jvmtiError WatchNotification(jvmtiEnv*, bool);
jvmtiError PinNotifications(jvmtiEnv*);
jvmtiError ReleaseNotifications(jvmtiEnv*);
void CommitNotifications();
}

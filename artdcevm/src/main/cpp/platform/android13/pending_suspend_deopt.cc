#include "platform/android13/pending_suspend_deopt.h"

namespace artdcevm::android13 {

bool PendingSuspendDeopt::Mark(void* thread) {
  if (!thread) return false;
  void* expected = nullptr;
  return thread_.compare_exchange_strong(expected, thread, std::memory_order_acq_rel);
}

bool PendingSuspendDeopt::IsPending(void* thread) const {
  return thread && thread_.load(std::memory_order_acquire) == thread;
}

bool PendingSuspendDeopt::Cancel(void* thread) {
  if (!thread) return false;
  return thread_.compare_exchange_strong(thread, nullptr, std::memory_order_acq_rel);
}

bool PendingSuspendDeopt::Consume(void* thread) {
  return Cancel(thread);
}

void PendingSuspendDeopt::Reset() {
  thread_.store(nullptr, std::memory_order_release);
}

}  // namespace artdcevm::android13

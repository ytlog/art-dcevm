#pragma once
#include <atomic>
#include <cstdint>
namespace artdcevm::android13 {
enum class SuspendDeoptState : uint8_t { kIdle, kPreparing, kQueued, kClaimed, kExecuting, kRejected, kCompleted };
// One outstanding request. Terminal states are retained until the coordinator
// releases them; claiming never acknowledges completion of the non-returning deopt.
class PendingSuspendDeopt {
 public:
  bool Mark(void* thread,uint64_t* token=nullptr);
  SuspendDeoptState State(void* thread,uint64_t token=0) const;
  bool IsPending(void* thread,uint64_t token=0) const;
  bool Cancel(void* thread,uint64_t token=0);
  bool Consume(void* thread,uint64_t* token=nullptr); // Queued -> Claimed.
  bool Execute(void* thread,uint64_t token=0);
  bool Reject(void* thread,uint64_t token=0);
  bool Complete(void* thread,uint64_t token=0);
  bool Release(void* thread,uint64_t token=0);
  bool InFlight() const;
  void Reset(); // Cancels queued/terminal requests only, never an in-flight request.
 private:
  bool Transition(void*,SuspendDeoptState,SuspendDeoptState,uint64_t token=0,uint64_t* claimed=nullptr);
  std::atomic<void*> thread_{nullptr};
  // Low byte is phase; upper bits are an ABA-resistant request generation.
  std::atomic<uint64_t> state_{0};
};
}

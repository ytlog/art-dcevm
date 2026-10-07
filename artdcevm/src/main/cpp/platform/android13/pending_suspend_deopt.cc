#include "platform/android13/pending_suspend_deopt.h"
namespace artdcevm::android13 {
namespace {
SuspendDeoptState Phase(uint64_t word) {return static_cast<SuspendDeoptState>(word&255);}
uint64_t WithPhase(uint64_t word,SuspendDeoptState phase) {return (word&~uint64_t{255})|static_cast<uint8_t>(phase);}
}
bool PendingSuspendDeopt::Mark(void* thread,uint64_t* token) {
  if(!thread) return false;
  auto expected=state_.load(std::memory_order_acquire);
  if(Phase(expected)!=SuspendDeoptState::kIdle && Phase(expected)!=SuspendDeoptState::kCompleted && Phase(expected)!=SuspendDeoptState::kRejected) return false;
  auto next=WithPhase(expected+256,SuspendDeoptState::kPreparing);
  if(!state_.compare_exchange_strong(expected,next,std::memory_order_acq_rel)) return false;
  thread_.store(thread,std::memory_order_relaxed);
  if(token) *token=next&~uint64_t{255};
  state_.store(WithPhase(next,SuspendDeoptState::kQueued),std::memory_order_release);
  return true;
}
SuspendDeoptState PendingSuspendDeopt::State(void* thread,uint64_t token) const {
  auto word=state_.load(std::memory_order_acquire);
  if((token && (word&~uint64_t{255})!=token) || !thread || Phase(word)==SuspendDeoptState::kPreparing || thread_.load(std::memory_order_relaxed)!=thread || state_.load(std::memory_order_acquire)!=word) return SuspendDeoptState::kIdle;
  return Phase(word);
}
bool PendingSuspendDeopt::Transition(void* thread,SuspendDeoptState from,SuspendDeoptState to,uint64_t token,uint64_t* claimed) {
  auto word=state_.load(std::memory_order_acquire);
  if((token && (word&~uint64_t{255})!=token) || !thread || Phase(word)!=from || thread_.load(std::memory_order_relaxed)!=thread) return false;
  if(!state_.compare_exchange_strong(word,WithPhase(word,to),std::memory_order_acq_rel)) return false;
  if(claimed) *claimed=word&~uint64_t{255};
  return true;
}
bool PendingSuspendDeopt::IsPending(void* thread,uint64_t token) const {
  auto state=State(thread,token);
  return state==SuspendDeoptState::kQueued || state==SuspendDeoptState::kClaimed || state==SuspendDeoptState::kExecuting;
}
bool PendingSuspendDeopt::Cancel(void* thread,uint64_t token) {return Transition(thread,SuspendDeoptState::kQueued,SuspendDeoptState::kRejected,token);}
bool PendingSuspendDeopt::Consume(void* thread,uint64_t* token) {return Transition(thread,SuspendDeoptState::kQueued,SuspendDeoptState::kClaimed,0,token);}
bool PendingSuspendDeopt::Execute(void* thread,uint64_t token) {return Transition(thread,SuspendDeoptState::kClaimed,SuspendDeoptState::kExecuting,token);}
bool PendingSuspendDeopt::Reject(void* thread,uint64_t token) {return Transition(thread,SuspendDeoptState::kClaimed,SuspendDeoptState::kRejected,token);}
bool PendingSuspendDeopt::Complete(void* thread,uint64_t token) {return Transition(thread,SuspendDeoptState::kExecuting,SuspendDeoptState::kCompleted,token);}
bool PendingSuspendDeopt::Release(void* thread,uint64_t token) {
  auto state=State(thread,token);
  if(state!=SuspendDeoptState::kRejected && state!=SuspendDeoptState::kCompleted) return false;
  return Transition(thread,state,SuspendDeoptState::kIdle,token);
}
bool PendingSuspendDeopt::InFlight() const {
  auto state=Phase(state_.load(std::memory_order_acquire));
  return state==SuspendDeoptState::kPreparing || state==SuspendDeoptState::kClaimed || state==SuspendDeoptState::kExecuting;
}
void PendingSuspendDeopt::Reset() {
  auto word=state_.load(std::memory_order_acquire);
  auto token=word&~uint64_t{255};
  void* thread=thread_.load(std::memory_order_relaxed);
  if(Phase(word)==SuspendDeoptState::kQueued) Cancel(thread,token);
  Release(thread,token);
}
}

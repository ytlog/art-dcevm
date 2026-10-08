#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/pending_suspend_deopt.h"
#include "platform/android13/suspend_deopt_art_abi.h"

#include "platform/symbols/android13.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/frame_recovery.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/runtime_access.h"
#include "platform/common/async_deopt_service.h"
#include <cstring>

namespace artdcevm::android13 {
namespace {
using SuspendFromCode = void (*)(void*);
SuspendFromCode original_suspend = nullptr;
suspend_abi::PushContext push_context = nullptr;
suspend_abi::Deoptimize deoptimize = nullptr;
std::atomic<uint64_t> suspend_deopt_hits{0};
PendingSuspendDeopt pending_deopt;
async_deopt_service::Service is_async;
// Only the owning thread writes this metadata, after claiming. Publication of
// Executing and Completed bounds its lifetime; caller TLS is never consulted.
void* selected_method = nullptr;
void** selected_quick = nullptr;
const void* selected_header = nullptr;
uint64_t selected_token = 0;
template<class T> T Read(const void* p,size_t at=0) {T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
struct Eligibility final : stack_abi::Visitor {
  explicit Eligibility(void* self):Visitor(self,nullptr,stack_abi::WalkKind::kSkipInlined) {}
  bool VisitFrame() override {
    void* method=Method();
    if(!method) return true;
    if(Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
       (art_method_abi::ReadAccessFlags(method)&0x100)) return true;
    bool async_allowed=false;
    if(quick_frame && header && is_async.Query(RuntimeInstance(),method,pc,&async_allowed) &&
       async_allowed && IsEvolutionPcRecoverable(thread,pc)) {
      allowed=true;selected_method=method;selected_quick=quick_frame;selected_header=header;
    }
    return false; // The first Java frame is precisely where ART can otherwise abort.
  }
  bool allowed=false;
};
struct Anchor final : stack_abi::Visitor {
  explicit Anchor(void* self):Visitor(self,nullptr,stack_abi::WalkKind::kSkipInlined) {}
  bool VisitFrame() override {
    if(quick_frame==selected_quick && header==selected_header) {present=true;return false;}
    return true;
  }
  bool present=false;
};
// Locked stack.h ShadowFrame link and ArtMethod prefix. These native frames
// contain managed register roots, but this observer neither reads nor moves them.
struct ShadowPrefix {void* link;void* method;};
static_assert(sizeof(ShadowPrefix)==16 && offsetof(ShadowPrefix,method)==8);

bool MarkPending(void* thread,uint64_t* token) { return pending_deopt.Mark(thread,token); }
bool IsPending(void* thread,uint64_t token) { return pending_deopt.IsPending(thread,token); }
void CancelPending(void* thread,uint64_t token) { pending_deopt.Cancel(thread,token); }
void ResetPending() { pending_deopt.Reset(); }
uint64_t Hits() { return suspend_deopt_hits.load(std::memory_order_acquire); }

void SuspendProxy(void* thread) {
  original_suspend(thread);
  if (!pending_deopt.Consume(thread,&selected_token)) return;
  Eligibility eligibility(thread); eligibility.Walk();
  if(!eligibility.allowed) {pending_deopt.Reject(thread,selected_token);return;}
  if(!pending_deopt.Execute(thread,selected_token)) return;
  suspend_deopt_hits.fetch_add(1, std::memory_order_relaxed);
  const suspend_abi::JValue result{0};
  push_context(thread, result, false, {0}, false, suspend_abi::MethodType::kDefault);
  deoptimize(thread);
}
}  // namespace

bool MarkEvolutionSuspendDeopt(void* thread,uint64_t* token) { return MarkPending(thread,token); }
bool EvolutionSuspendDeoptPending(void* thread,uint64_t token) { return IsPending(thread,token); }
SuspendDeoptState EvolutionSuspendDeoptState(void* thread,uint64_t token) {return pending_deopt.State(thread,token);}
bool EvolutionSuspendDeoptInFlight() {return pending_deopt.InFlight();}
bool ReleaseEvolutionSuspendDeopt(void* thread,uint64_t token) {return pending_deopt.Release(thread,token);}
void AcknowledgeEvolutionSuspendDeopt(void* thread,void* frame,bool from_code) {
  // This callback is synchronous on the deoptimizing ART thread. A previous
  // generation's entry cannot arrive after the same thread has entered a newer
  // SuspendProxy: it was acknowledged before calling the original interpreter.
  // Cross-thread/coordinator notifications still carry explicit generation tokens.
  if(from_code || !frame || pending_deopt.State(thread)!=SuspendDeoptState::kExecuting) return;
  bool found=false;
  for(void* current=frame;current;current=Read<void*>(current,offsetof(ShadowPrefix,link))) {
    if(Read<void*>(current,offsetof(ShadowPrefix,method))==selected_method) {found=true;break;}
  }
  if(!found) return;
  // Thread::DeoptimizeWithDeoptimizationException clears its quick top and
  // installs the shadow top before this entry. artDeoptimizeFromCompiledCode
  // also installs a new managed fragment. Partial deopt may retain LOWER quick
  // frames, so this only acknowledges the selected anchor; Drain rescans all threads.
  Anchor anchor(thread);anchor.Walk();
  if(!anchor.present) pending_deopt.Complete(thread,selected_token);
}
void CancelEvolutionSuspendDeopt(void* thread,uint64_t token) { CancelPending(thread,token); }
void ResetEvolutionSuspendDeopt() { ResetPending(); }
uint64_t EvolutionSuspendDeoptHits() { return Hits(); }

bool InstallSuspendDeoptHook(HookEngine& hooks, std::string* error) {
  auto push = reinterpret_cast<suspend_abi::PushContext>(
      hooks.FindSymbol("libart.so", cas::kPushDeoptimizationContext));
  auto deopt = reinterpret_cast<suspend_abi::Deoptimize>(
      hooks.FindSymbol("libart.so", cas::kDeoptimize));
  void* target = hooks.FindSymbol("libart.so", cas::kTestSuspendFromCode);
  // This installer remains behind the calibrated Android 13 profile. The
  // declaration is selected explicitly; no SDK or symbol-fallback ABI guess.
  constexpr auto declaration=async_deopt_service::Declaration::kPcOnly;
  async_deopt_service::Service async;
  bool async_bound=async_deopt_service::Service::Bind(declaration,
      hooks.FindSymbol("libart.so",async_deopt_service::Service::SymbolName(declaration)),&async);
  if (!push || !deopt || !target || !async_bound) {
    *error = "Missing named suspend entrypoint or deoptimization capability";
    return false;
  }
  is_async = async;
  push_context = push;
  deoptimize = deopt;
  return hooks.Install(target, reinterpret_cast<void*>(SuspendProxy),
                       reinterpret_cast<void**>(&original_suspend), error) != nullptr;
}

}  // namespace artdcevm::android13

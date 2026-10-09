#pragma once
#include "api/hook_engine.h"
#include <jvmti.h>
#include <atomic>
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/runtime_services.h"
#include "platform/common/transaction_root_view.h"
namespace artdcevm::native_source_class_loading {
class Run;
namespace detail {struct Hooks;}
// Actual source class-loading callback lifecycle; this is NOT a mutator/GC stop.
// The original constructor waits for in-progress definitions after AddCallback.
// Only entry to the actual same-Run first Finish leaf witnesses completion of that
// wait. The typed vtable and primitive prefix alone never establish completion.
class Service final {
 public:
 static bool Bind(HookEngine&,const runtime_services::Snapshot&,Service*,std::string*);
 bool ObservedOriginal()const noexcept{return observed_.load(std::memory_order_acquire);}
 private:
 friend class Run;friend bool Install(HookEngine&,Service&,std::string*);
 void*runtime_=nullptr;const void*vtable_=nullptr;std::atomic<bool> observed_{false};
};
class Run final {
 public:
 Run(Service&,void* actual_driver,const runnable_jni::Context&);
 ~Run();Run(const Run&)=delete;Run&operator=(const Run&)=delete;
 static Run* Current()noexcept;
 // Called only at the sole original Finish hook entry, after exact Run/Holder
 // provenance checks. Private allocation inside Finish needs the completed wait.
 bool BeforeFinish(void* actual_owner,const transaction_root::Holder*,const void* actual_iterator)noexcept;
 void AfterFinish(void* actual_owner,bool original_result)noexcept;
 bool Paused()const noexcept;
 bool BeginCleanup(void* actual_carrier)noexcept;
 bool EndCleanup(void* actual_carrier)noexcept;
 bool CleanupCompleted()const noexcept;
 bool OwnsDriver(const void*)const noexcept;
 // Call only after the actual source Run returned; the original Remove must have returned
 // before Run's private Holder/DEX destruction. Unwind never proves success;
 // no source pointer is read here.
 bool Complete(bool original_success)noexcept;
 private:
 friend struct detail::Hooks;
 void Added(void* actual_callbacks,void* actual_pauser)noexcept;
 bool Removing(void* actual_callbacks,void* actual_pauser)noexcept;
 void Removed(void* actual_callbacks,void* actual_pauser,bool returned)noexcept;
 Service&service_;void*driver_;runnable_jni::Context context_;Run*previous_=nullptr;
 void*cleanup_carrier_=nullptr;bool cleanup_started_=false,cleanup_ended_=false;
 void*callbacks_=nullptr;void*pauser_=nullptr;void*finish_owner_=nullptr;
 const transaction_root::Holder*holder_=nullptr;int32_t next_=0;
 enum class Removal {kNone,kPending,kReturned,kUnwound};
 Removal removal_=Removal::kNone;
 bool active_=false,added_=false,wait_complete_=false,failed_=false,complete_=false;
};
// Named Add/Remove callback leaf hooks, installed under actual SuspendAll by
// their sole owner. They preserve the originals and observe only Run's TLS.
bool Install(HookEngine&,Service&,std::string*);
}

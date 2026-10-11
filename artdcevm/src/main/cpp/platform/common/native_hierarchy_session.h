#pragma once
#include "platform/common/native_hierarchy_allocator.h"
#include "platform/common/native_hierarchy_source_cache.h"
#include "platform/common/native_hierarchy_source_checks.h"

namespace artdcevm::native_hierarchy_private_ledger {class Ledger;}
namespace artdcevm::native_hierarchy_session {
// The sole original Redefiner::Run owner creates this object before calling Run.
// Dependencies and their calibrated owner services outlive Run. No source Holder,
// iterator or DEX is retained after ReleaseAfterRun. Session never owns a source
// pause/GC lifetime: final proof remains with the independent source stop owner.
struct Bindings {
 void* driver=nullptr;
 // Borrow the actual synchronous Bridge callback object. A copy has matching
 // fields but cannot borrow the original Runnable JNI scope.
 const runnable_jni::Context*context=nullptr;
 HookEngine* hooks=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 source_handle_roots::Service* roots=nullptr;
 source_handle_roots::Pool* pool=nullptr;
 native_hierarchy_graph::Authority* authority=nullptr;
 native_hierarchy_allocator::Service* allocation=nullptr;
 native_hierarchy_source_cache::AllocatorBatchAccess* access=nullptr;
 native_hierarchy_source_checks::Scope::Refuse refuse=nullptr;
 native_hierarchy_source_checks::FailureObserver failure_observer=nullptr;
 native_hierarchy_private_ledger::Ledger* ledger=nullptr;
 void* refund_owner=nullptr;
 // Runs after original Finish/verification views unwind and before any private
 // Class, native method array or DexCache is retired. The execution owner must
 // remove its journaled entries and reject live replacement frames/foreign
 // Instrumentation references while all source roots and DEX remain alive.
 // This prerequisite is distinct from the notification after disposal below.
 bool (*before_private_discard)(void*,const char**refusal)noexcept=nullptr;
 // After every private record query, before original Holder/DEX destruction.
 // Releases operation-owned roots only; scope headers remain linked until Run
 // returns. This is not a substitute for external-reference/no-escape proof.
 void* private_roots_owner=nullptr;
 bool (*release_private_roots)(void*,const char**refusal)noexcept=nullptr;
 void (*refund_complete)(void*)noexcept=nullptr;
};
class Session final {
 public:
 // Construct OUTSIDE the rejectable source-stop scope, so that the original
 // WaitingGC/Runnable restoration and owned reservation rollback complete
 // before destruction dispatches a pending refund. It grants no stop rights.
 class RefundDeferral final {
  public:
  explicit RefundDeferral(Session&)noexcept;
  ~RefundDeferral();
  RefundDeferral(const RefundDeferral&)=delete;RefundDeferral&operator=(const RefundDeferral&)=delete;
  bool Ready()const noexcept{return owner_!=nullptr;}
  private:Session*owner_=nullptr;
 };
 explicit Session(const Bindings&);
 ~Session();
 Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 static Session* Current()noexcept;
 bool Ready()const noexcept;
 bool Finish(void* owner,transaction_root::Holder*,void* iterator,
             bool(*original)(void*,transaction_root::Holder*,void*),std::string*);
 void* Allocate(void* owner,const void* old_handle,const void* cache_handle,
                uint16_t index,std::string*);
 // Called only by the sole owned-removal verification leaf after it validates
 // actual Holder/owner/order. The iterator is borrowed for this original call.
 bool BeforeVerification(void* owner,const void* iterator,std::string*);
 void AfterVerification(void* owner,const void* iterator,bool original_result)noexcept;
 // Ordinary Collected phase, before any stop or native strong registration.
 bool PrepareCollected(const source_commit::Plan&,std::string*);
 bool Collected(const source_commit::Plan&,std::string*);
 void Refund()noexcept;
 void BeginCommit(const source_commit::Plan&)noexcept;
 // Must follow the original Run return. Any failure must already have refunded
 // within a fallible source boundary, BEFORE Run destroys its DEX/Holder.
 bool ReleaseAfterRun(bool original_success)noexcept;
 // Scalar cleanup receipt only. No source storage is inspected after Run.
 bool ReleasedAfterFailure(const void*driver)const noexcept{
  return ready_&&released_&&failed_&&refunded_&&execution_refunded_&&roots_released_&&!committed_&&
   bindings_.driver==driver;
 }
 bool OwnsDriver(const void*)const noexcept;
 bool OwnsAuthority(const native_hierarchy_graph::Authority&authority)const noexcept{
  return Ready()&&bindings_.authority==&authority;
 }
 bool OwnsClassOwner(const void*)const noexcept;
 void LoadClass(void*,void*,const void*,const void*,const void*)noexcept;
 bool EnsureInstanceIds(const void*)noexcept;
 private:
 bool Match(void*,const void*,transaction_root::Iterator*)const noexcept;
 void Refuse(void*,int,const char*)noexcept;
 Bindings bindings_;
 Session* previous_=nullptr;
 std::unique_ptr<native_hierarchy_source_checks::Scope> checks_;
 std::unique_ptr<native_hierarchy_allocator::Batch> batch_;
 transaction_root::Holder* holder_=nullptr;
 transaction_root::Metadata metadata_{};
 size_t finish_next_=0,verify_next_=0;
 void* finishing_=nullptr;void* verification_owner_=nullptr;
 const void* verification_iterator_=nullptr;
 bool attached_=false,refunded_=false,execution_refunded_=false;
 bool roots_released_=false;
 size_t refund_deferrals_=0;
 bool ready_=false,failed_=false,sealed_=false,committed_=false,released_=false;
};
// Ordinary unchanged-hierarchy fixture transaction, activated by the sole Run
// owner. The named allocator hook observes its REAL original allocation; it
// never creates an extra calibration Class or bypasses original source checks.
class CalibrationScope final {
 public:
 CalibrationScope(void* actual_driver,const runnable_jni::Context&,
                  jobject strong_fixture,source_handle_roots::Service&,
                  native_hierarchy_allocator::Service&);
 ~CalibrationScope();
 CalibrationScope(const CalibrationScope&)=delete;
 CalibrationScope&operator=(const CalibrationScope&)=delete;
 static CalibrationScope* Current()noexcept;
 void* Allocate(void* owner,const void* old_handle,const void* parent_handle,
                const void* cache_handle,uint16_t index,
                abi::arm64::android_common::HierarchyAllocator original);
 private:
 void* driver_;const runnable_jni::Context&context_;jobject fixture_;
 source_handle_roots::Service&roots_;native_hierarchy_allocator::Service&allocation_;
 CalibrationScope*previous_=nullptr;bool active_=false;
};
// Shares the same first-Finish proxy with Install, without requiring allocator
// services. Ordinary calibration may install this first; later hierarchy
// installation reuses its original trampoline and never hooks the leaf twice.
// Call under the production owner's actual ScopedSuspendAll.
bool InstallPauseObservation(HookEngine&,std::string*);
// Installs only named fixed allocator/first-Finish/capture leaves. It never
// installs Run, CheckVerification or Collect observers (their owner is unique).
// Call under the production owner's actual ScopedSuspendAll.
bool Install(HookEngine&,native_hierarchy_allocator::Service&,
             const native_hierarchy_allocator::Functions&,std::string*);
}

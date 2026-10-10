#pragma once
#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/common/native_member_provider.h"
#include "platform/common/prepared_root_registry.h"
#include "platform/common/whole_process_frames.h"
#include <array>
#include <memory>
namespace artdcevm::native_required_cleanup {
class Facade;
// Private typed source closure: exact named functions and actual owners, bound
// outside the stop. It is necessary evidence, never final commit authority.
class Binding final {
 public:
  static bool Bind(HookEngine&,JNIEnv*,const native_member_provider::Dependencies&,Binding*,std::string*);
 private:
  friend class Service;
  bool Current(JavaVM*,void*)const noexcept;
  bool Matches(const native_source_commit::Services&,const native_member_provider::Service&)const noexcept;
  bool CopiesCurrent(const source_commit::Plan&,const modern_field_metadata::ReadContext&,const native_source_commit::Services&)const noexcept;
  JavaVM* vm_=nullptr;void* (*current_)()=nullptr;
  native_member_provider::Dependencies dependencies_;
  std::array<void*,8> symbols_{};
};
// One original Run owner. All JNI registration, scanner storage and native
// services must already be prepared. Continuous Session ownership, a complete
// physical frame walk and the actual provider are necessary independent proofs.
class Service final:public native_hierarchy_private_ledger::CleanupOwner {
 public:
  Service(const Binding&,void* actual_self,void* actual_driver,
          native_source_stop::Session&,const whole_process_frames::Service&,
          whole_process_frames::Service::Prepared&,const native_cache::Service&,
          void* actual_jit,native_member_provider::Service&,prepared_roots::Registry&);
  bool AttachHolder(const transaction_root::Holder*,const transaction_root::Metadata&) noexcept;
  ~Service();Service(const Service&)=delete;Service&operator=(const Service&)=delete;
  // Called only in the actual Collected stop AFTER all root registration.
  // Constructor/allocation occur here, never in the final original boundary.
  std::unique_ptr<Facade> Prepare(std::unique_ptr<source_commit::Services>,
                                 source_commit::Plan&,std::string*);
  // Before native provider construction, establish actual collected frame/root
  // proof from this pre-bound closure. No delegate is inferred at this stage.
  bool UseRecoveryContext(const modern_field_metadata::ReadContext&) noexcept;
  bool PrepareCollected(source_commit::Plan&,std::string*);
  const whole_process_frames::StoppedRecoveryProof& RecoveryProof()const noexcept{return recovery_proof_;}
  bool RefreshFinal(const prepared_roots::Resolver&) noexcept;
  // The sole Source Run observer invokes this after successful facade.Validate
  // and before Hierarchy Session/Ledger BeginCommit. No validation is repeated.
  bool CommitObserver(const source_commit::Plan&) noexcept;
  bool Verify(void*,const transaction_root::Holder*,const source_commit::Plan&)const noexcept override;
  bool IsStopped(void*,const transaction_root::Holder*)const noexcept override;
  bool IsFinalWindow(void*,const transaction_root::Holder*)const noexcept override;
  // Readonly collected-owner/copy witness; grants no final or publication permission.
  bool VerifyFinalCopies(const source_commit::Plan&,const modern_field_metadata::ReadContext&,void*,const transaction_root::Holder*,const native_source_commit::Services&)const noexcept;
  bool VerifyPreparedCopies(const source_commit::Plan&,const modern_field_metadata::ReadContext&,void*,const transaction_root::Holder*,const native_source_commit::Services&)const noexcept;
  void BeginCommit(const source_commit::Plan&)const noexcept override;
  bool Completed()const noexcept;
  // Readonly witness of the actual full Facade/observer/ledger BeginCommit path.
  bool MutationBegun(const source_commit::Plan&)const noexcept;
  const whole_process_frames::Report& Frames()const noexcept{return frames_report_;}
 private:
  friend class Facade;
  bool HolderCurrent()const noexcept;
  bool Exact(const source_commit::Plan&)const noexcept;
  bool ValidateFinal(const source_commit::Plan&)const;
  void Finish(const source_commit::Plan&)const noexcept;
  const Binding&binding_;JavaVM*vm_;void*self_;void*driver_;const transaction_root::Holder*holder_=nullptr;
  native_source_stop::Session&stop_;const whole_process_frames::Service&frames_;
  whole_process_frames::Service::Prepared&frame_storage_;const native_cache::Service&cache_;
  void*jit_;native_member_provider::Service&provider_;prepared_roots::Registry&registry_;
  transaction_root::Holder holder_header_{};transaction_root::Metadata holder_metadata_;
  source_commit::Plan*plan_=nullptr;source_commit::Plan expected_;prepared_roots::Bindings roots_;
  native_source_commit::Services*delegate_=nullptr;
  const modern_field_metadata::ReadContext* recovery_context_=nullptr;
  mutable whole_process_frames::StoppedRecoveryProof recovery_proof_;
  mutable whole_process_frames::Report frames_report_;
  enum class Phase{kEmpty,kCollected,kFinalRefreshed,kValidated,kObserver,kLedger,kDelegating,kCompleted};
  mutable Phase phase_=Phase::kEmpty;
};
// Concrete native delegate; factory cannot install an empty cleanup callback.
// Own this preallocated facade through the whole final source_commit::Run.
class Facade final:public source_commit::Services {
 public:
  ~Facade();native_source_commit::Services& Delegate()noexcept{return *delegate_;}
  bool Validate(const source_commit::Plan&)const override;
  void BeginCommit(const source_commit::Plan&)const override;
  uint32_t Status(void*)const override;void SetStatus(void*,uint32_t)const override;
  void CopyFields(bool,void*,void*,void*,void*)const override;void* ObjectClass(void*)const override;
  void MarkClassAndMethodsObsolete(void*,void*)const override;
  void UpdateReflectiveReferences(const source_commit::Plan&)const override;
  void ForceDeoptimizeFrames()const override;void ReplaceObjectReferences()const override;
  void RestoreDeclaringClasses(const source_commit::Plan&)const override;
  void* NewClassAfterReplacement(void*,size_t)const override;void RetainOldClass(void*,void*)const override;
  void InvalidateCompiledCode()const override;void DeoptimizeEverythingForAbstractTransition()const override;
  void ClearInterpreterCaches()const override;
 private:
  friend class Service;
  Facade(Service&,std::unique_ptr<native_source_commit::Services>);
  void RequireMutation()const noexcept;
  Service&owner_;std::unique_ptr<native_source_commit::Services>delegate_;
  // These witnesses follow actual delegate returns, never callback installation.
  mutable size_t static_copies_=0,instance_copies_=0,obsolete_=0,retained_=0;
  mutable unsigned cleanup_step_=0;
  mutable void* pending_retained_=nullptr;
};
}

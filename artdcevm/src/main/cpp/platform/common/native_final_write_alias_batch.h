#pragma once
#include "platform/common/native_final_write_access.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/source_commit.h"
#include "platform/common/native_schema_guard_verifier.h"
#include <array>
namespace artdcevm::final_write_guard {
// One operation owns the public handles before entering the original Runnable
// executor, and closes them after it returns to that same ordinary Native caller.
// Publishing transfers only used weak handles to NativeAccess's sole alias Set.
class AliasBatch final:public native_schema_guard_verifier::FinalFields {
 public:
 static std::unique_ptr<AliasBatch>Prepare(NativeAccess&,JNIEnv*,
     const source_handle_roots::Service&,const std::vector<jclass>&,std::string*);
 // Ordinary Native only. Deactivate expired leases before forgetting/deleting
 // their weak handles; never wait for a reader while holding shared mutator.
 static bool Cleanup(NativeAccess&,JNIEnv*,const source_handle_roots::Service&,std::string*);
 ~AliasBatch()override;AliasBatch(const AliasBatch&)=delete;AliasBatch&operator=(const AliasBatch&)=delete;
 // No JNI/safepoint; use actual Collected Plan and concrete method services.
 // All public handle creation/registration has already completed in Prepare.
 bool Bind(const runnable_jni::Context&,const native_source_stop::Session&,const source_commit::Plan&,
     const std::vector<modern_methods::Service*>&,
     const modern_field_metadata::ReadContext&,std::string*);
 // Same actual stopped operation and Plan; reread current roots after refresh.
 bool Verify(const source_commit::Plan&,const native_source_stop::Session&,void*self,
     const modern_field_metadata::ReadContext&)const noexcept;
 void Publish(const source_commit::Plan&,const native_source_stop::Session&,void*self,
     const modern_field_metadata::ReadContext&)const noexcept;
 // The same operation owner supplies ordinary permission checks, stopped
 // root refresh, and final publication; there is no second permission plan.
 bool Verify(const source_commit::Plan&,size_t,void* loader,
     const evolution::ClassSchema&,const evolution::ClassSchema&,
     const native_schema_policy::Requirement&,std::string*)const override;
 bool RefreshPlan(const source_commit::Plan&,std::string*)const override;
 bool VerifyAliases(const source_commit::Plan&,std::string*)const override;
 void BeginCommit(const source_commit::Plan&)const override;
 // Close after original Run/executor unwinds. Pending exceptions are preserved.
 bool Close()noexcept;
 bool Published()const noexcept{return published_;}
 bool UsesMetadata(const modern_field_metadata::FieldMetadata&metadata)const noexcept{return &access_.metadata_==&metadata;}
 private:
 struct Root {jobject strong=nullptr;jweak weak=nullptr;bool strong_registered=false,weak_registered=false,transferred=false;modern_methods::Service*methods=nullptr;};
 struct Record {size_t root=0;modern_methods::Service*methods=nullptr;};
 struct Permission {size_t pair;void*before;void*after;bool statics;uint32_t before_flags,after_flags,before_index,after_index;};
 enum class Phase {Prepared,Binding,Bound,Published,Closed};
 AliasBatch(NativeAccess&access,JNIEnv*env,const source_handle_roots::Service&roots,JavaVM*vm,void*self)
  :access_(access),env_(env),roots_(roots),vm_(vm),self_(self){}
 bool Native()const noexcept;
 bool Ordinary()const noexcept;
 bool Permissions(const source_commit::Plan&,const modern_field_metadata::ReadContext&)const noexcept;
 bool Match(const source_commit::Plan&,void*self,const modern_field_metadata::ReadContext&)const noexcept;
 bool Member(const final_field_aliases::Alias&,const Record&,void*klass,
     const modern_field_metadata::ReadContext&)const noexcept;
 NativeAccess&access_;JNIEnv*env_;const source_handle_roots::Service&roots_;JavaVM*vm_;void*self_;
 mutable std::vector<Root>handles_;std::vector<Record>records_;
 mutable std::vector<Permission>permissions_;
 mutable std::unique_ptr<final_field_aliases::Node>node_;
 const runnable_jni::Context*context_=nullptr;
 const native_source_stop::Session*stop_=nullptr;
 const source_commit::Plan*plan_=nullptr;
 const modern_field_metadata::ReadContext*read_=nullptr;
 std::array<const void*,5>storage_{};std::array<size_t,5>counts_{};
 Guard::ProofWitness proof_;
 mutable Phase phase_=Phase::Prepared;mutable bool published_=false;
};
}

#pragma once
#include "platform/common/native_final_write_access.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/source_commit.h"
#include <array>
namespace artdcevm::final_write_guard {
// One operation owns the public handles before entering the original Runnable
// executor, and closes them after it returns to that same ordinary Native caller.
// Publishing transfers only used weak handles to NativeAccess's sole alias Set.
class AliasBatch final {
 public:
 static std::unique_ptr<AliasBatch>Prepare(NativeAccess&,JNIEnv*,
     const source_handle_roots::Service&,const std::vector<jclass>&,std::string*);
 // Ordinary Native only. Deactivate expired leases before forgetting/deleting
 // their weak handles; never wait for a reader while holding shared mutator.
 static bool Cleanup(NativeAccess&,JNIEnv*,const source_handle_roots::Service&,std::string*);
 ~AliasBatch();AliasBatch(const AliasBatch&)=delete;AliasBatch&operator=(const AliasBatch&)=delete;
 // No JNI/safepoint; use actual Collected Plan and concrete method services.
 // All public handle creation/registration has already completed in Prepare.
 bool Bind(const runnable_jni::Context&,const source_commit::Plan&,
     const std::vector<modern_methods::Service*>&,
     const modern_field_metadata::ReadContext&,std::string*);
 // Same actual stopped operation and Plan; reread current roots after refresh.
 bool Verify(const source_commit::Plan&,const native_source_stop::Session&,void*self,
     const modern_field_metadata::ReadContext&)const noexcept;
 void Publish(const source_commit::Plan&,const native_source_stop::Session&,void*self,
     const modern_field_metadata::ReadContext&)noexcept;
 // Close after original Run/executor unwinds. Pending exceptions are preserved.
 bool Close()noexcept;
 bool Published()const noexcept{return published_;}
 private:
 struct Root {jobject strong=nullptr;jweak weak=nullptr;bool strong_registered=false,weak_registered=false,transferred=false;modern_methods::Service*methods=nullptr;};
 struct Record {size_t root=0;modern_methods::Service*methods=nullptr;};
 enum class Phase {Prepared,Binding,Bound,Published,Closed};
 AliasBatch(NativeAccess&access,JNIEnv*env,const source_handle_roots::Service&roots,JavaVM*vm,void*self)
  :access_(access),env_(env),roots_(roots),vm_(vm),self_(self){}
 bool Native()const noexcept;
 bool Match(const source_commit::Plan&,void*self,const modern_field_metadata::ReadContext&)const noexcept;
 bool Member(const final_field_aliases::Alias&,const Record&,void*klass,
     const modern_field_metadata::ReadContext&)const noexcept;
 NativeAccess&access_;JNIEnv*env_;const source_handle_roots::Service&roots_;JavaVM*vm_;void*self_;
 std::vector<Root>handles_;std::vector<Record>records_;
 std::unique_ptr<final_field_aliases::Node>node_;
 const source_commit::Plan*plan_=nullptr;
 const modern_field_metadata::ReadContext*read_=nullptr;
 std::array<const void*,5>storage_{};std::array<size_t,5>counts_{};
 Guard::ProofWitness proof_;
 Phase phase_=Phase::Prepared;bool published_=false;
};
}

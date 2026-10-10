#pragma once
#include "platform/common/native_typed_heap_walk.h"
#include "platform/common/bounded_managed_roots.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_commit.h"
#include "platform/common/native_required_cleanup.h"
namespace artdcevm::native_hierarchy_typed_storage {
// Captures public declared types before native borrowing, and evaluates the
// actual final paused heap against one original source Plan. It grants no
// receiver/AOT/array-cache permission; those owners remain independently required.
class Proof final {
 public:
 Proof(const native_typed_heap_walk::Service&,const source_handle_roots::Service&,
       const modern_field_metadata::FieldMetadata&);
 ~Proof();Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 bool Stage(HookEngine&,const runnable_jni::Context&,jvmtiEnv*,std::string*);
 bool Staged()const noexcept;
 bool Capture(const source_commit::Plan&,const prepared_roots::Registrar&,std::string*);
 bool CanRefresh(const prepared_roots::Resolver&)const noexcept;
 void Refresh(const prepared_roots::Resolver&)noexcept;
 // Runs the same complete heap inspection in the actual collected stop after
 // cleanup owns the concrete delegate. Final Validate still repeats the walk.
 bool ValidatePrepared(const source_commit::Plan&,const modern_field_metadata::ReadContext&,
       const native_required_cleanup::Service&,void* actual_driver,const transaction_root::Holder*,
       const native_source_commit::Services&)const noexcept;
 bool Validate(const source_commit::Plan&,const modern_field_metadata::ReadContext&,
       const native_required_cleanup::Service&,void* actual_driver,const transaction_root::Holder*,
       const native_source_commit::Services&)const noexcept;
 void Clear(JNIEnv*);
 enum class Refusal {None,Owner,Inventory,SourceCopy,UnknownCell,IncompatibleField,IncompatibleArray};
 Refusal LastRefusal()const noexcept;
 private:
 bool ValidateHeap(const source_commit::Plan&,const modern_field_metadata::ReadContext&,
       const native_source_commit::Services&)const noexcept;
 struct State;std::unique_ptr<State> state_;
};
}

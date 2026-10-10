#pragma once
#include "platform/common/native_receiver_service.h"
#include "platform/common/native_hierarchy_typed_storage.h"
#include "platform/common/native_hierarchy_graph.h"
namespace artdcevm::native_narrowing {
// One refundable graph operation. Permanent dispatch is necessary but cannot
// authorize publication without exact retained-cell and active-frame proofs.
class Authority final {
 public:
 Authority(receiver_guard::Guard&,const receiver_guard::FrameReader&,native_hierarchy_typed_storage::Proof&);
 Authority(const Authority&)=delete;Authority&operator=(const Authority&)=delete;
 bool Prepare(const native_hierarchy_graph::Graph&g,std::string*e){return operations_.prepare(*this,g,e);}
 // Bound by the actual original Run after its concrete native delegate exists.
 bool BindRun(const native_required_cleanup::Service&cleanup,void*actual_driver,
              const transaction_root::Holder*h,const native_source_commit::Services&s)noexcept{return operations_.bind(*this,cleanup,actual_driver,h,s);}
 bool Validate(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p,
               const modern_field_metadata::ReadContext&c)const noexcept{return operations_.validate(*this,g,p,c);}
 void BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{operations_.commit(*this,g,p);}
 private:
 // Immutable implementation dispatch, constructed only by this concrete owner.
 // No caller-supplied callbacks can replace any typed final proof.
 struct Operations {
 bool(*prepare)(Authority&,const native_hierarchy_graph::Graph&,std::string*);
 bool(*bind)(Authority&,const native_required_cleanup::Service&,void*,const transaction_root::Holder*,const native_source_commit::Services&)noexcept;
 bool(*validate)(const Authority&,const native_hierarchy_graph::Graph&,const source_commit::Plan&,const modern_field_metadata::ReadContext&)noexcept;
 void(*commit)(const Authority&,const native_hierarchy_graph::Graph&,const source_commit::Plan&)noexcept;
 };
 static bool PrepareImpl(Authority&,const native_hierarchy_graph::Graph&,std::string*);
 static bool BindImpl(Authority&,const native_required_cleanup::Service&,void*,const transaction_root::Holder*,const native_source_commit::Services&)noexcept;
 static bool ValidateImpl(const Authority&,const native_hierarchy_graph::Graph&,const source_commit::Plan&,const modern_field_metadata::ReadContext&)noexcept;
 static void CommitImpl(const Authority&,const native_hierarchy_graph::Graph&,const source_commit::Plan&)noexcept;
 const Operations operations_;
 receiver_guard::PermanentDispatch dispatch_;native_hierarchy_typed_storage::Proof&storage_;
 const native_hierarchy_graph::Graph*graph_=nullptr;
 const native_required_cleanup::Service*cleanup_=nullptr;void*driver_=nullptr;
 const transaction_root::Holder*holder_=nullptr;const native_source_commit::Services*services_=nullptr;
 mutable const source_commit::Plan*validated_=nullptr;mutable bool committed_=false;
};
}

#include "platform/common/native_narrowing_authority.h"
#include <cstdlib>
namespace artdcevm::native_narrowing {
Authority::Authority(receiver_guard::Guard&g,const receiver_guard::FrameReader&f,native_hierarchy_typed_storage::Proof&s):operations_{PrepareImpl,BindImpl,ValidateImpl,CommitImpl},dispatch_(g,f),storage_(s){}
bool Authority::PrepareImpl(Authority&owner,const native_hierarchy_graph::Graph&graph,std::string*error){
 if(owner.graph_||graph.nodes.empty()||graph.inputs.empty()||!owner.storage_.Staged()){if(error)*error="Narrowing lacks fresh complete typed-storage inventory";return false;}
 if(!owner.dispatch_.Prepare(error))return false;owner.graph_=&graph;return true;
}
bool Authority::BindImpl(Authority&owner,const native_required_cleanup::Service&cleanup,void*driver,const transaction_root::Holder*holder,const native_source_commit::Services&services)noexcept{
 if(!owner.graph_||owner.cleanup_||!driver||!holder||owner.validated_||owner.committed_)return false;owner.cleanup_=&cleanup;owner.driver_=driver;owner.holder_=holder;owner.services_=&services;return true;
}
bool Authority::ValidateImpl(const Authority&owner,const native_hierarchy_graph::Graph&graph,const source_commit::Plan&plan,const modern_field_metadata::ReadContext&context)noexcept{
 owner.validated_=nullptr;if(owner.committed_||owner.graph_!=&graph||!owner.cleanup_||!owner.driver_||!owner.holder_||!owner.services_||!context.owner||!context.extent||!owner.cleanup_->IsFinalWindow(owner.driver_,owner.holder_))return false;
 // Both proofs consume the actual current original cleanup window and exact
 // immutable Plan. Original async=false is preserved; no callback enables AOT.
 if(!owner.storage_.Validate(plan,context,*owner.cleanup_,owner.driver_,owner.holder_,*owner.services_)||!owner.cleanup_->RecoveryProof().ValidateFinal(plan)||!owner.dispatch_.Validate())return false;
 owner.validated_=&plan;return true;
}
void Authority::CommitImpl(const Authority&owner,const native_hierarchy_graph::Graph&graph,const source_commit::Plan&plan)noexcept{
 if(owner.graph_!=&graph||owner.validated_!=&plan||owner.committed_)std::abort();owner.dispatch_.BeginCommit();owner.committed_=true;
}
}

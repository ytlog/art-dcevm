#include "platform/common/native_call_graph_inventory.h"
#include "platform/common/native_dex_identity.h"
#include "platform/common/native_dex_instruction_width.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>
namespace artdcevm::native_call_graph_inventory {
namespace {
native_call_graph::InvokeKind Kind(receiver_guard::InvokeKind k){
 using K=receiver_guard::InvokeKind;using O=native_call_graph::InvokeKind;
 switch(k){case K::Direct:return O::Direct;case K::Static:return O::Static;case K::Super:return O::Super;
 case K::Virtual:return O::Virtual;case K::Interface:return O::Interface;case K::Custom:return O::Custom;
 default:return O::Polymorphic;}
}
}
bool Inventory::Prepare(const runnable_jni::Context&context,const native_schema_guard_verifier::Verifier&verifier,
 const receiver_guard::SourceProducer&source,const std::vector<void*>&changed,bool changed_fields,std::string*error){
 const auto deny=[&](const char*s){if(error)*error=s;return false;};
 if(prepared_||verifier_||!nodes_.empty()||!runnable_jni::Bridge::BorrowOriginalScope(context))return deny("call-graph-original-runnable-window-unavailable");
 source_=&source;self_=context.self;
 if(!verifier.CaptureCallGraph(context,source,this,error))return false;
 verifier_=&verifier;
 auto&ordinals=method_ordinals_;ordinals.reserve(methods_.size());
 for(size_t i=0;i<methods_.size();++i)if(!ordinals.emplace(methods_[i].native.method,i).second)return deny("call-graph-native-method-ambiguous");
 for(void*seed:changed){auto found=ordinals.find(seed);
  if(found==ordinals.end())return deny("call-graph-seed-outside-current-inventory");seeds_.push_back(found->second);
 }
 // Actual field sites are matched to independently staged affected field
 // name/type candidates. Inherited reference owners never exclude a caller.
 if(changed_fields)for(size_t i=0;i<methods_.size();++i)if(methods_[i].fields)seeds_.push_back(i);
 using Key=std::tuple<const void*,uint64_t,uint32_t>;
 std::map<Key,size_t> declarations;
 for(size_t i=0;i<methods_.size();++i)if(methods_[i].declaring==methods_[i].containing){
  const auto&id=nodes_[i].identity;
  if(!declarations.emplace(Key{id.dex_namespace,id.loader_token,methods_[i].native.dex_index},i).second)return deny("call-graph-source-declaration-ambiguous");
 }
 for(size_t caller=0;caller<methods_.size();++caller)for(const auto&site:methods_[caller].calls){
  const auto kind=Kind(site.instruction.kind);size_t target=SIZE_MAX;
  if(kind==native_call_graph::InvokeKind::Direct||kind==native_call_graph::InvokeKind::Static){
   const auto&from=nodes_[caller].identity;
   auto found=declarations.find(Key{from.dex_namespace,from.loader_token,site.instruction.index});
   if(found!=declarations.end()){const auto&m=methods_[found->second];
    const bool is_static=(m.native.modifiers&8u)!=0;
    if(is_static==(kind==native_call_graph::InvokeKind::Static)&&
       native_dex_identity::DexIdentity(methods_[caller].code.source_dex).Matches(
         site.instruction.index,m.owner,m.name,m.signature))target=found->second;
   }
  }
  edges_.push_back({caller,target,site.dex_pc,kind,target==SIZE_MAX?
   native_call_graph::Coverage::Unresolved:native_call_graph::Coverage::Exact});
 }
 std::sort(seeds_.begin(),seeds_.end());seeds_.erase(std::unique(seeds_.begin(),seeds_.end()),seeds_.end());
 plan_=native_call_graph::Build(nodes_,edges_,seeds_);
 if(plan_.failure!=native_call_graph::Failure::None)return deny("call-graph-immutable-inventory-invalid");
 for(auto i:plan_.interpret)selection_.push_back(methods_[i].native);
 prepared_=true;return true;
}
bool Inventory::Prepare(const runnable_jni::Context&context,const native_schema_guard_verifier::Verifier&verifier,
 const receiver_guard::SourceProducer&source,std::string*error){
 if(!Prepare(context,verifier,source,{},true,error))return false;
 seeds_.insert(seeds_.end(),affected_seeds_.begin(),affected_seeds_.end());
 std::sort(seeds_.begin(),seeds_.end());seeds_.erase(std::unique(seeds_.begin(),seeds_.end()),seeds_.end());
 plan_=native_call_graph::Build(nodes_,edges_,seeds_);selection_.clear();
 for(auto i:plan_.interpret)selection_.push_back(methods_[i].native);
 return plan_.failure==native_call_graph::Failure::None;
}
bool Inventory::ValidateOrdinary(const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&resolved_&&verifier_&&roots_&&roots_->Generation()==generation_&&
  verifier_->ValidateCallGraphOrdinary(*this,read);
}
bool Inventory::ValidateOrdinary(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&resolved_&&verifier_&&roots_&&roots_->Generation()==generation_&&
  verifier_->ValidateCallGraphSource(*this,read,false,&stop);
}
bool Inventory::VisitCalls(ResolutionVisitor visitor,void*owner)const{
 if(!prepared_||!visitor)return false;
 for(const auto&m:methods_)for(const auto&site:m.calls)
  if(!visitor(owner,{m.native.method,m.code,m.bytes,site}))return false;
 return true;
}
bool Inventory::ResolveCalls(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,std::string*error){
 return ResolveCallsImpl(context,source,nullptr,error);
}
bool Inventory::ResolveCalls(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,const native_call_graph_super::Resolver&super,std::string*error){
 return ResolveCallsImpl(context,source,&super,error);
}
bool Inventory::ResolveCallsImpl(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,const native_call_graph_super::Resolver*super,std::string*error){
 const auto deny=[&](const char*s){if(error)*error=s;return false;};
 if(!prepared_||resolved_||sealed_||source_!=&source||context.self!=self_||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||native_source_class_loading::Run::Current())
  return deny("call-graph-resolution-requires-original-prepause-window");
 std::vector<native_call_graph_resolution::Result> results;
 super_resolver_=super;
 for(size_t caller=0;caller<methods_.size();++caller){const auto&m=methods_[caller];for(size_t pc=0;pc<m.calls.size();++pc){const auto&site=m.calls[pc];
  if(site.instruction.kind==receiver_guard::InvokeKind::Custom||site.instruction.kind==receiver_guard::InvokeKind::Polymorphic)continue;
  if(pc>=m.related.size())return deny("call-graph-reference-candidate-inventory-invalid");
  if(!m.related[pc]){results.push_back({});continue;}
  auto result=native_call_graph_resolution::ResolveOriginal(context,source,m.native.method,m.code,m.bytes,site);
  if(result.status==native_call_graph_resolution::Status::OriginalException)return deny("call-graph-original-resolution-exception-pending");
  if(super&&site.instruction.kind==receiver_guard::InvokeKind::Super&&result.status==native_call_graph_resolution::Status::Resolved&&
     !verifier_->ResolveCallGraphSuper(context,source,*super,caller,pc,result.declaration,this,error))return false;
  results.push_back(result);
 }}
 if(!verifier_->ApplyCallGraphResolution(context,results,this,error))return false;
 plan_=native_call_graph::Build(nodes_,edges_,seeds_);
 if(plan_.failure!=native_call_graph::Failure::None)return deny("call-graph-resolved-inventory-invalid");
 selection_.clear();for(auto i:plan_.interpret)selection_.push_back(methods_[i].native);
 resolved_=true;return true;
}
bool Inventory::SealLoading(const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read)noexcept{
 return SealLoadingImpl(loading,driver,read,nullptr);
}
bool Inventory::SealLoading(const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read,const native_source_stop::Session&stop)noexcept{
 return SealLoadingImpl(loading,driver,read,&stop);
}
bool Inventory::SealLoadingImpl(const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read,const native_source_stop::Session*stop)noexcept{
 if(sealed_||!prepared_||!resolved_||!driver||!loading.Paused()||!loading.OwnsDriver(driver)||
    !verifier_||!roots_||roots_->Generation()!=generation_||!verifier_->ValidateCallGraphSource(*this,read,true,stop))return false;
 loading_=&loading;driver_=driver;sealed_=true;return true;
}
bool Inventory::Validate(const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&sealed_&&verifier_&&loading_&&loading_->Paused()&&loading_->OwnsDriver(driver_)&&
  roots_&&roots_->Generation()==generation_&&verifier_->ValidateCallGraph(*this,read);
}
bool Inventory::Validate(const modern_field_metadata::ReadContext&read,const native_source_stop::Session&stop)const noexcept{
 return prepared_&&sealed_&&verifier_&&loading_&&loading_->Paused()&&loading_->OwnsDriver(driver_)&&
  roots_&&roots_->Generation()==generation_&&verifier_->ValidateCallGraphSource(*this,read,true,&stop);
}
}

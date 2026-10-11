#include "platform/common/native_call_graph_replacement.h"
#include "platform/common/native_method_table.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/jvmti_method_flags.h"
#include "platform/common/readable_mapping_scope.h"
#include <cstring>
#include <unordered_map>

namespace artdcevm::native_call_graph_replacement {
namespace {
template<class T>T Read(const void*p,size_t at=0){T result;std::memcpy(&result,static_cast<const unsigned char*>(p)+at,sizeof(result));return result;}
bool Mapped(const modern_field_metadata::ReadContext&read,const void*p,size_t need)noexcept{
 size_t extent=0;return p&&read.owner&&read.extent&&read.extent(read.owner,p,&extent)&&extent>=need;
}
}
bool Binding::Stage(HookEngine&hooks,const runnable_jni::Context&context,jvmtiEnv*ti,
 const native_source_class_loading::Run&loading,const void*driver,
 const native_owned_removal::RootedGraph&graph,size_t partition,size_t index,
 const native_call_graph_incoming::Inventory&incoming,bounded_managed_roots::Store&roots,
 const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&methods,
 const receiver_guard::SourceProducer&source,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(attempted_)return deny("call-graph-replacement-repeated-stage");attempted_=true;
 if(!ti||!context.env||!context.self||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    !loading.OwnsDriver(driver)||!loading.OwnsFinishedHolder(graph.holder,graph.metadata.count)||
    partition>=graph.partitions.size()||!incoming.Schema())
  return deny("call-graph-replacement-original-finished-Holder-scope-unavailable");
 const auto&part=graph.partitions[partition];
 if(!part.old_classes||!part.new_classes||!part.class_count||index>=part.class_count||
    part.class_count>size_t(INT32_MAX)||part.holder_index>=graph.metadata.count)
  return deny("call-graph-replacement-Holder-partition-invalid");
 auto*env=context.env;
 struct Locals {JNIEnv*env;jobject old=nullptr,replacement=nullptr,loader=nullptr;
  ~Locals(){for(auto p:{old,replacement,loader})if(p)env->DeleteLocalRef(p);}}locals{env};
 if(env->ExceptionCheck()||env->GetArrayLength(part.old_classes)!=jsize(part.class_count)||
    env->GetArrayLength(part.new_classes)!=jsize(part.class_count)||env->ExceptionCheck())
  return deny("call-graph-replacement-public-array-shape-refused");
 locals.old=env->GetObjectArrayElement(part.old_classes,jsize(index));
 locals.replacement=env->GetObjectArrayElement(part.new_classes,jsize(index));
 if(!locals.old||!locals.replacement||env->ExceptionCheck()||env->IsSameObject(locals.old,locals.replacement)||
    ti->GetClassLoader(static_cast<jclass>(locals.replacement),&locals.loader)!=JVMTI_ERROR_NONE||env->ExceptionCheck())
  return deny("call-graph-replacement-public-Class-or-loader-refused");
 if(!roots.Stage(context,part.old_classes,&old_array_,error)||!roots.Stage(context,part.new_classes,&new_array_,error)||
    !roots.Stage(context,locals.old,&old_class_,error)||!roots.Stage(context,locals.replacement,&new_class_,error)||
    (locals.loader&&!roots.Stage(context,locals.loader,&loader_,error)))return false;
 incoming_=&incoming;roots_=&roots;generation_=roots.Generation();metadata_=&metadata;source_=&source;
 loading_=&loading;driver_=driver;holder_=graph.holder;self_=context.self;owner_count_=graph.metadata.count;
 holder_index_=part.holder_index;class_index_=index;class_count_=part.class_count;
 root_=reinterpret_cast<RootBarrier>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 method_identity::ScopedReadableMappings mappings;
 if(!mappings.Ready())return deny("call-graph-replacement-current-mappings-unavailable");
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 void*klass=nullptr;
 if(!root_||!metadata.Calibrated()||!roots.Read(new_class_,&klass)||!HolderArrays(read))
  return deny("call-graph-replacement-original-native-Holder-arrays-refused");
 native_method_table::View table;
 if(!native_method_table::Read(metadata,methods,klass,&table,error,&read)||!incoming.MatchesNamespace(table.dex))
  return deny("call-graph-replacement-independent-incoming-namespace-refused");
 // Copied/default methods need their independent declaring Class/source graph
 // join. Never silently omit them and report this Class completely bound.
 if(table.methods.count!=incoming.Methods().size())return deny("call-graph-replacement-copied-method-join-unproved");
 layout_=methods.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 array_=table.methods.array;
 std::unordered_map<uint32_t,void*>by_index;
 for(uint32_t n=0;n<table.methods.count;++n){method_metadata::Identity identity{};
  if(!method_metadata::ReadIdentity(layout_,table.methods.At(n),layout_.size,&identity)||
     !by_index.emplace(identity.dex_method_index,table.methods.At(n)).second)return deny("call-graph-replacement-native-method-index-ambiguous");
 }
 for(const auto&m:incoming.Methods()){
  const auto found=by_index.find(m.declaration.dex_index);
  if(found==by_index.end())return deny("call-graph-replacement-incoming-method-missing");
  const uint32_t flags=method_metadata::ReadAccessFlags(found->second);
  if(jvmti_method_flags::Public(flags)!=jvmti_method_flags::Public(m.declaration.access))
   return deny("call-graph-replacement-incoming-modifiers-mismatch");
  declared_.push_back({found->second,m.declaration.dex_index,flags,bool(m.code_offset)});
  if(m.code_offset)selection_.push_back({found->second,new_class_,new_class_,m.declaration.dex_index,flags&0xffffu});
 }
 if(!Check(read))return deny("call-graph-replacement-fresh-native-method-body-refused");
 prepared_=true;if(error)error->clear();return true;
}
bool Binding::HolderArrays(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!roots_||roots_->Generation()!=generation_||!loading_||native_source_class_loading::Run::Current()!=loading_||!loading_->OwnsDriver(driver_)||
    !loading_->OwnsFinishedHolder(holder_,owner_count_)||!root_||!metadata_||
    !Mapped(read,holder_,sizeof(transaction_root::Holder)))return false;
 transaction_root::Holder holder{};
 if(!transaction_root::ReadHolderHeader(holder_,sizeof(holder),&holder)||!Mapped(read,holder.array_handle,4))return false;
 void*array=root_(const_cast<uint32_t*>(holder.array_handle));
 if(owner_count_>size_t(INT32_MAX)/abi::arm64::android11::kRootSlots||
    !Mapped(read,array,12+owner_count_*abi::arm64::android11::kRootSlots*4)||
    Read<int32_t>(array,8)!=int32_t(owner_count_*abi::arm64::android11::kRootSlots))return false;
 void*old_array=nullptr;void*new_array=nullptr;void*old=nullptr;void*replacement=nullptr;
 if(!roots_->Read(old_array_,&old_array)||!roots_->Read(new_array_,&new_array)||
    !roots_->Read(old_class_,&old)||!roots_->Read(new_class_,&replacement)||old==replacement||
    !Mapped(read,old_array,12+class_count_*4)||!Mapped(read,new_array,12+class_count_*4)||
    Read<int32_t>(old_array,8)!=int32_t(class_count_)||Read<int32_t>(new_array,8)!=int32_t(class_count_))return false;
 const auto base=holder_index_*abi::arm64::android11::kRootSlots;
 return metadata_->Reference(array,12+4*(base+abi::arm64::android11::kOldClassesSlot))==old_array&&
        metadata_->Reference(array,12+4*(base+abi::arm64::android11::kNewClassesSlot))==new_array&&
        metadata_->Reference(old_array,12+4*class_index_)==old&&metadata_->Reference(new_array,12+4*class_index_)==replacement;
}
bool Binding::Check(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!HolderArrays(read)||!incoming_||!source_)return false;
 void*klass=nullptr;void*loader=nullptr;
 if(!roots_->Read(new_class_,&klass)||!roots_->Read(loader_,&loader))return false;
 evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
 if(!native_interface_dispatch::ReadCurrentNamespace(*metadata_,klass,read,&dex,&named)||
    !incoming_->MatchesNamespace(dex)||named.loader!=loader)return false;
 const auto flags=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?offsetof(class_metadata::Class11,access_flags):offsetof(class_metadata::Class16,access_flags);
 if(!Mapped(read,klass,flags+4)||Read<uint32_t>(klass,flags)&0x40000u||
    (Read<uint32_t>(klass,flags)&0xffffu)!=(incoming_->Schema()->access&0xffffu)||!Mapped(read,klass,56)||
    Read<uint64_t>(klass,48)!=uintptr_t(array_))return false;
 if(array_){if(!Mapped(read,array_,8+declared_.size()*layout_.size)||Read<uint32_t>(array_)!=declared_.size())return false;}
 else if(!declared_.empty())return false;
 for(const auto&m:declared_){method_metadata::Identity identity{};
  if(!Mapped(read,m.method,layout_.size)||!native_selective_interpretation::detail::Member(array_,8+declared_.size()*layout_.size,m.method,layout_.size)||
     !method_metadata::ReadIdentity(layout_,m.method,layout_.size,&identity)||identity.dex_method_index!=m.index||
     identity.access_flags!=m.flags||(identity.access_flags&0x40000u)||metadata_->MethodDeclaringClass(m.method)!=klass)return false;
  if(m.body){receiver_guard::CodeView code{};
   if(!source_->Code(self_,m.method,&code,read)||code.source_dex.begin!=dex.begin||code.source_dex.data_begin!=dex.data_begin)return false;
   code.source_dex.class_def_index=dex.class_def_index;
   if(!incoming_->MatchesCode(m.index,code))return false;
  }
 }
 return true;
}
bool Binding::Validate(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&stop.IsStopped(self_)&&Check(read)&&stop.IsStopped(self_);
}
bool Batch::Stage(HookEngine&hooks,const runnable_jni::Context&context,jvmtiEnv*ti,
 const native_source_class_loading::Run&loading,const void*driver,
 const native_owned_removal::RootedGraph&graph,const std::vector<Definition>&definitions,
 bounded_managed_roots::Store&roots,const modern_field_metadata::FieldMetadata&metadata,
 const method_identity::Calibration&methods,const receiver_guard::SourceProducer&source,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(attempted_)return deny("call-graph-replacement-batch-repeated-stage");attempted_=true;
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||!loading.OwnsDriver(driver)||
    !loading.OwnsFinishedHolder(graph.holder,graph.metadata.count)||graph.partitions.empty())
  return deny("call-graph-replacement-batch-original-Holder-unavailable");
 std::vector<std::vector<bool>>covered;std::unordered_map<size_t,size_t>owners;
 size_t count=0;
 for(const auto&p:graph.partitions){
  if(!p.class_count||p.class_count>size_t(INT32_MAX)||p.holder_index>=graph.metadata.count||
     !owners.emplace(p.holder_index,covered.size()).second||p.class_count>SIZE_MAX-count)
   return deny("call-graph-replacement-batch-partition-shape-invalid");
  covered.emplace_back(p.class_count,false);count+=p.class_count;
 }
 if(count!=definitions.size())return deny("call-graph-replacement-batch-incoming-coverage-incomplete");
 for(const auto&d:definitions){
  if(!d.incoming||!d.incoming->Schema()||d.partition>=covered.size()||
     d.class_index>=covered[d.partition].size()||covered[d.partition][d.class_index])
   return deny("call-graph-replacement-batch-definition-ambiguous");
  covered[d.partition][d.class_index]=true;
 }
 for(const auto&d:definitions){auto binding=std::make_unique<Binding>();
  if(!binding->Stage(hooks,context,ti,loading,driver,graph,d.partition,d.class_index,*d.incoming,
                    roots,metadata,methods,source,error))return false;
  bindings_.push_back(std::move(binding));
 }
 // No JNI after this snapshot. Recheck earlier bindings after later roots were
 // staged, rather than trusting a managed address retained across those calls.
 method_identity::ScopedReadableMappings mappings;
 if(!mappings.Ready())return deny("call-graph-replacement-batch-current-mappings-unavailable");
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 std::unordered_map<void*,bool>classes,native;
 for(const auto&b:bindings_){void*old=nullptr;void*replacement=nullptr;
  if(!b->Check(read)||!roots.Read(b->old_class_,&old)||!roots.Read(b->new_class_,&replacement)||
     !classes.emplace(old,false).second||!classes.emplace(replacement,true).second)
   return deny("call-graph-replacement-batch-Class-alias-or-fresh-binding-refused");
  for(const auto&m:b->declared_)if(!native.emplace(m.method,true).second)
   return deny("call-graph-replacement-batch-native-method-alias-refused");
  selection_.insert(selection_.end(),b->Selection().begin(),b->Selection().end());
 }
 roots_=&roots;generation_=roots.Generation();
 if(!BuildGraph(read,error))return false;
 if(!CheckGraph(read))return deny("call-graph-replacement-rooted-outgoing-graph-refused");
 prepared_=true;if(error)error->clear();return true;
}
bool Batch::BuildGraph(const modern_field_metadata::ReadContext&read,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 std::vector<size_t>seeds;
 for(size_t i=0;i<bindings_.size();++i){const auto&b=*bindings_[i];
  void*klass=nullptr;void*loader=nullptr;evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  if(!b.Check(read)||!roots_->Read(b.new_class_,&klass)||!roots_->Read(b.loader_,&loader)||
     !native_interface_dispatch::ReadCurrentNamespace(*b.metadata_,klass,read,&dex,&named))
   return deny("call-graph-replacement-outgoing-current-Class-unproved");
  uint64_t token=0;
  if(loader){
   size_t index=0;
   for(;index<loaders_.size();++index){void*other=nullptr;
    if(!roots_->Read(loaders_[index],&other))return deny("call-graph-replacement-outgoing-loader-unproved");
    if(loader==other)break;
   }
   if(index==loaders_.size())loaders_.push_back(b.loader_);
   token=index+1;
  }
  const size_t base=nodes_.size();
  std::unordered_map<uint32_t,size_t>indices;
  for(size_t n=0;n<b.declared_.size();++n){const auto&m=b.declared_[n];
   const size_t ordinal=nodes_.size();
   if(!indices.emplace(m.index,n).second)return deny("call-graph-replacement-outgoing-index-ambiguous");
   nodes_.push_back({{dex.begin,token,m.method},(jvmti_method_flags::Public(m.flags)&0x100u)?native_call_graph::MethodKind::Native:native_call_graph::MethodKind::Managed});
   node_bindings_.push_back({i,n});if(m.body)seeds.push_back(ordinal);
  }
  // Incoming facts and native declarations have the same independently
  // checked order/index. DEX indices never join across namespaces or loaders.
  const auto&methods=b.incoming_->Methods();
  for(size_t n=0;n<methods.size();++n)for(const auto&call:methods[n].calls){
   const auto kind=native_call_graph::Kind(call.site.instruction.kind);size_t target=SIZE_MAX;
   if((kind==native_call_graph::InvokeKind::Direct||kind==native_call_graph::InvokeKind::Static)&&
      call.reference.declaring_class==b.incoming_->Schema()->descriptor){
    auto found=indices.find(call.site.instruction.index);
    if(found!=indices.end()){const size_t candidate=found->second;const auto&m=methods[candidate];
     const bool is_static=(m.declaration.access&8u)!=0;
     const bool direct=m.direct&&!is_static&&((m.declaration.access&2u)||m.declaration.name=="<init>");
     if(!(m.declaration.access&0x400u)&&call.reference.name==m.declaration.name&&
        call.reference.descriptor==m.declaration.descriptor&&
        ((kind==native_call_graph::InvokeKind::Static&&m.direct&&is_static)||
         (kind==native_call_graph::InvokeKind::Direct&&direct)))target=base+candidate;
    }
   }
   edges_.push_back({base+n,target,call.site.dex_pc,kind,target==SIZE_MAX?
    native_call_graph::Coverage::Unresolved:native_call_graph::Coverage::Exact});
  }
 }
 plan_=native_call_graph::Build(nodes_,edges_,seeds);
 return plan_.failure==native_call_graph::Failure::None||deny("call-graph-replacement-outgoing-graph-invalid");
}
bool Batch::CheckGraph(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!roots_||roots_->Generation()!=generation_||nodes_.size()!=node_bindings_.size())return false;
 for(size_t i=0;i<loaders_.size();++i){void*loader=nullptr;
  if(!roots_->Read(loaders_[i],&loader)||!loader)return false;
  for(size_t j=0;j<i;++j){void*other=nullptr;if(!roots_->Read(loaders_[j],&other)||other==loader)return false;}
 }
 for(size_t i=0;i<nodes_.size();++i){const auto&binding=node_bindings_[i];
  if(binding.binding>=bindings_.size())return false;const auto&b=*bindings_[binding.binding];
  if(binding.declared>=b.declared_.size())return false;
  const auto&node=nodes_[i];void*klass=nullptr;void*loader=nullptr;evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  if(!roots_->Read(b.new_class_,&klass)||!roots_->Read(b.loader_,&loader)||
     !native_interface_dispatch::ReadCurrentNamespace(*b.metadata_,klass,read,&dex,&named)||
     node.identity.dex_namespace!=dex.begin||node.identity.native_method!=b.declared_[binding.declared].method)return false;
  const auto token=node.identity.loader_token;
  if(!token){if(loader)return false;}
  else {void*representative=nullptr;
   if(token>loaders_.size()||!roots_->Read(loaders_[token-1],&representative)||representative!=loader)return false;
  }
 }
 return true;
}
bool Batch::Validate(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 if(!prepared_||!roots_||roots_->Generation()!=generation_)return false;
 for(size_t i=0;i<bindings_.size();++i){const auto&b=*bindings_[i];
  if(!b.Validate(stop,read))return false;
  void*old=nullptr;void*replacement=nullptr;
  if(!roots_->Read(b.old_class_,&old)||!roots_->Read(b.new_class_,&replacement))return false;
  for(size_t j=0;j<i;++j){void*other_old=nullptr;void*other_new=nullptr;
   if(!roots_->Read(bindings_[j]->old_class_,&other_old)||!roots_->Read(bindings_[j]->new_class_,&other_new)||
      old==other_old||old==other_new||replacement==other_old||replacement==other_new)return false;
  }
 }
 return CheckGraph(read)&&stop.IsStopped(bindings_.front()->self_);
}
}

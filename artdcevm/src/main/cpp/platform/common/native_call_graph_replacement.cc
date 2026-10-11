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
 struct Locals {JNIEnv*env;jobject old=nullptr,replacement=nullptr,loader=nullptr,parent=nullptr;
  ~Locals(){for(auto p:{old,replacement,loader,parent})if(p)env->DeleteLocalRef(p);}}locals{env};
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
 locals.parent=env->GetSuperclass(static_cast<jclass>(locals.replacement));
 if(env->ExceptionCheck()||(locals.parent&&!roots.Stage(context,locals.parent,&parent_,error)))return false;
 jint interface_count=0;jclass*interfaces=nullptr;
 const auto interface_result=ti->GetImplementedInterfaces(static_cast<jclass>(locals.replacement),&interface_count,&interfaces);
 struct Interfaces {JNIEnv*env;jvmtiEnv*ti;jint count;jclass*values;
  ~Interfaces(){if(values)for(jint n=0;n<count;++n)if(values[n])env->DeleteLocalRef(values[n]);
   if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}}interface_locals{env,ti,interface_count,interfaces};
 if(interface_result!=JVMTI_ERROR_NONE||interface_count<0||env->ExceptionCheck())return deny("call-graph-replacement-public-interfaces-unavailable");
 if(interface_count&&!interfaces)return deny("call-graph-replacement-public-interface-array-invalid");
 for(jint n=0;n<interface_count;++n){bounded_managed_roots::Slot slot;
  if(!interfaces[n]||!roots.Stage(context,interfaces[n],&slot,error))return false;interfaces_.push_back(slot);
 }
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
 if(table.methods.count<incoming.Methods().size())return deny("call-graph-replacement-declared-array-truncated");
 layout_=methods.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 array_=table.methods.array;method_count_=table.methods.count;
 std::unordered_map<uint32_t,void*>by_index;
 for(uint32_t n=0;n<incoming.Methods().size();++n){method_metadata::Identity identity{};
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
 for(uint32_t n=uint32_t(incoming.Methods().size());n<method_count_;++n){method_metadata::Identity identity{};
  void*method=table.methods.At(n);
  if(!method_metadata::ReadIdentity(layout_,method,layout_.size,&identity)||identity.dex_method_index==UINT32_MAX||
     (identity.access_flags&0x81000000u)!=0x01000000u||
     (identity.access_flags&(0x40000u|0x10000u|0x100u|0x8u|0x2u)))
   return deny("call-graph-replacement-copied-tail-identity-refused");
  copied_.push_back({{method,identity.dex_method_index,identity.access_flags,false},{}});
 }
 if(!Check(read,Purpose::Forward,true))return deny("call-graph-replacement-fresh-native-method-body-refused");
 prepared_=copied_.empty();if(error)error->clear();return true;
}
bool Binding::HolderArrays(const modern_field_metadata::ReadContext&read,Purpose purpose)const noexcept{
 if(!roots_||roots_->Generation()!=generation_||!loading_||native_source_class_loading::Run::Current()!=loading_||!loading_->OwnsDriver(driver_)||
    !(purpose==Purpose::Forward?loading_->OwnsFinishedHolder(holder_,owner_count_):loading_->OwnsRefundHolder(holder_,owner_count_,read))||!root_||!metadata_||
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
bool Binding::Check(const modern_field_metadata::ReadContext&read,Purpose purpose,bool pending_copies)const noexcept{
 if(!HolderArrays(read,purpose)||!incoming_||!source_)return false;
 void*klass=nullptr;void*loader=nullptr;
 if(!roots_->Read(new_class_,&klass)||!roots_->Read(loader_,&loader))return false;
 evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
 if(!native_interface_dispatch::ReadCurrentNamespace(*metadata_,klass,read,&dex,&named)||
    !incoming_->MatchesNamespace(dex)||named.loader!=loader)return false;
 void*parent=nullptr;
 if(!roots_->Read(parent_,&parent))return false;
 if(incoming_->Schema()->access&0x200u){
  field_copy::CopyClassView object{};
  if(parent||!named.parent||!metadata_->ClassIdentityVerified(named.parent,"Ljava/lang/Object;",nullptr,nullptr,nullptr,&object,nullptr,&read)||object.loader)return false;
 }else if(named.parent!=parent)return false;
 const auto flags=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?offsetof(class_metadata::Class11,access_flags):offsetof(class_metadata::Class16,access_flags);
 if(!Mapped(read,klass,flags+4)||Read<uint32_t>(klass,flags)&0x40000u||
    (Read<uint32_t>(klass,flags)&0xffffu)!=(incoming_->Schema()->access&0xffffu)||!Mapped(read,klass,56)||
    Read<uint64_t>(klass,48)!=uintptr_t(array_))return false;
 if(method_count_!=(declared_.size()+copied_.size())||method_count_>(SIZE_MAX-8)/layout_.size)return false;
 const size_t array_bytes=8+size_t(method_count_)*layout_.size;
 if(array_){if(!Mapped(read,array_,array_bytes)||Read<uint32_t>(array_)!=method_count_)return false;}
 else if(!declared_.empty())return false;
 for(const auto&m:declared_){method_metadata::Identity identity{};
  if(!Mapped(read,m.method,layout_.size)||!native_selective_interpretation::detail::Member(array_,array_bytes,m.method,layout_.size)||
     !method_metadata::ReadIdentity(layout_,m.method,layout_.size,&identity)||identity.dex_method_index!=m.index||
     identity.access_flags!=m.flags||(identity.access_flags&0x40000u)||metadata_->MethodDeclaringClass(m.method)!=klass)return false;
  if(m.body){receiver_guard::CodeView code{};
   if(!source_->Code(self_,m.method,&code,read)||code.source_dex.begin!=dex.begin||code.source_dex.data_begin!=dex.data_begin)return false;
   code.source_dex.class_def_index=dex.class_def_index;
   if(!incoming_->MatchesCode(m.index,code))return false;
  }
 }
 for(const auto&copy:copied_){const auto&m=copy.native;method_metadata::Identity identity{};
  if(!Mapped(read,m.method,layout_.size)||!native_selective_interpretation::detail::Member(array_,array_bytes,m.method,layout_.size)||
     !method_metadata::ReadIdentity(layout_,m.method,layout_.size,&identity)||
     identity.dex_method_index!=m.index||identity.access_flags!=m.flags)return false;
  if(pending_copies)continue;
  if(copy.incoming_owner){const auto&owner=*copy.incoming_owner;
   if(copy.source.declaration||copy.source.member||!before_||before_->RootStore()!=roots_||
      owner.roots_!=roots_||owner.generation_!=generation_||owner.loading_!=loading_||owner.driver_!=driver_||
      owner.holder_!=holder_||owner.self_!=self_||copy.incoming_ordinal>=owner.declared_.size()||
      copy.incoming_ordinal>=owner.incoming_->Methods().size()||
      !(owner.incoming_->Schema()->access&0x200u)||!owner.Check(read,purpose,true))return false;
   const auto&original=owner.declared_[copy.incoming_ordinal];
   const auto&member=owner.incoming_->Methods()[copy.incoming_ordinal].declaration;
   const auto&declaration=copy.incoming_declaration;void*declaring=nullptr;void*containing=nullptr;
   if(!roots_->Read(declaration.declaring_root,&declaring)||!declaring||declaring==klass||
      !roots_->Read(owner.new_class_,&containing)||declaring!=containing||
      declaration.method!=original.method||declaration.dex_index!=m.index||m.body!=original.body||
      !before_->MatchesDeclaration(m.method,declaration,member,read))return false;
   if(m.body){receiver_guard::CodeView code{},expected{};
    if(!source_->Code(self_,m.method,&code,read)||!owner.incoming_->Code(m.index,&expected))return false;
    code.source_dex.class_def_index=expected.source_dex.class_def_index;
    if(!owner.incoming_->MatchesCode(m.index,code))return false;
   }
   continue;
  }
  native_call_graph_inventory::Inventory::DeclarationSource original;
  void*declaring=nullptr;
  if(!before_||before_->RootStore()!=roots_||!before_->BorrowDeclaration(m.method,read,&original)||
     original.declaration!=copy.source.declaration||original.member!=copy.source.member||
     !original.declaration||!roots_->Read(original.declaration->declaring_root,&declaring)||!declaring||declaring==klass)return false;
  if(!Mapped(read,declaring,flags+4)||!(Read<uint32_t>(declaring,flags)&0x200u)||
     Read<uint32_t>(declaring,flags)&0x40000u)return false;
  if(m.body){receiver_guard::CodeView code{};
   if(!original.code||!original.bytes||!source_->Code(self_,m.method,&code,read)||
      code.source_dex.begin!=original.code->source_dex.begin||code.code_item!=original.code->code_item||
      code.instructions!=original.code->instructions||code.units>SIZE_MAX/2||
      original.bytes->size()!=code.units*2||std::memcmp(original.bytes->data(),code.instructions,original.bytes->size()))return false;
  }else if(original.code||original.bytes||original.calls)return false;
 }
 return true;
}
bool Binding::Validate(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateFor(stop,read,Purpose::Forward);
}
bool Binding::ValidateRefund(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateFor(stop,read,Purpose::Refund);
}
bool Binding::ValidateFor(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read,Purpose purpose)const noexcept{
 return prepared_&&stop.IsStopped(self_)&&Check(read,purpose)&&stop.IsStopped(self_);
}
bool Batch::Stage(HookEngine&hooks,const runnable_jni::Context&context,jvmtiEnv*ti,
 const native_source_class_loading::Run&loading,const void*driver,
 const native_owned_removal::RootedGraph&graph,const std::vector<Definition>&definitions,
 bounded_managed_roots::Store&roots,const modern_field_metadata::FieldMetadata&metadata,
 const method_identity::Calibration&methods,const receiver_guard::SourceProducer&source,std::string*error,
 const native_call_graph_inventory::Inventory*before){
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
 for(auto&b:bindings_)if(!b->copied_.empty()){
  if(!before||before->RootStore()!=&roots||!before->ValidateOrdinary(context,read))
   return deny("call-graph-replacement-copied-source-census-unavailable");
  b->before_=before;
  for(auto&copy:b->copied_){
   void*declaring=metadata.MethodDeclaringClass(copy.native.method);
   if(!declaring)return deny("call-graph-replacement-copied-declaring-root-unavailable");
   const Binding*incoming_owner=nullptr;
   for(const auto&candidate:bindings_){void*replacement=nullptr;
    if(!roots.Read(candidate->new_class_,&replacement))return deny("call-graph-replacement-copied-new-source-root-unavailable");
    if(replacement==declaring){if(incoming_owner)return deny("call-graph-replacement-copied-new-source-ambiguous");incoming_owner=candidate.get();}
   }
   if(incoming_owner){
    if(incoming_owner==b.get()||!(incoming_owner->incoming_->Schema()->access&0x200u)||
       !incoming_owner->Check(read,Binding::Purpose::Forward,true))
     return deny("call-graph-replacement-copied-new-interface-source-refused");
    size_t ordinal=SIZE_MAX;native_selective_interpretation::Method declaration;
    for(size_t n=0;n<incoming_owner->declared_.size();++n){const auto&original=incoming_owner->declared_[n];
     const auto&member=incoming_owner->incoming_->Methods()[n].declaration;
     native_selective_interpretation::Method candidate{original.method,incoming_owner->new_class_,incoming_owner->new_class_,
       original.index,jvmti_method_flags::Public(original.flags)};
     if(before->MatchesDeclaration(copy.native.method,candidate,member,read)){
      if(ordinal!=SIZE_MAX)return deny("call-graph-replacement-copied-new-declaration-ambiguous");
      ordinal=n;declaration=candidate;
     }
    }
    if(ordinal==SIZE_MAX)return deny("call-graph-replacement-copied-new-declaration-unproved");
    copy.incoming_owner=incoming_owner;copy.incoming_ordinal=ordinal;copy.incoming_declaration=declaration;
    copy.native.body=incoming_owner->declared_[ordinal].body;
   }else{
    if(!before->BorrowDeclaration(copy.native.method,read,&copy.source)||!copy.source.member||
       !copy.source.declaration)return deny("call-graph-replacement-copied-declaration-join-unproved");
    for(const auto&owner:bindings_){void*retiring=nullptr;
     if(!roots.Read(owner->old_class_,&retiring))return deny("call-graph-replacement-copied-retiring-root-unavailable");
     if(declaring==retiring)return deny("call-graph-replacement-copied-retiring-source-projection-unproved");
    }
    copy.native.body=copy.source.code!=nullptr;
   }
   if(copy.native.body)b->selection_.push_back({copy.native.method,copy.Declaration()->declaring_root,
     b->new_class_,copy.native.index,jvmti_method_flags::Public(copy.native.flags)});
  }
  if(!b->Check(read))return deny("call-graph-replacement-copied-body-join-refused");
  b->prepared_=true;
 }
 std::unordered_map<void*,bool>classes,native;
 for(const auto&b:bindings_){void*old=nullptr;void*replacement=nullptr;
  if(!b->Check(read)||!roots.Read(b->old_class_,&old)||!roots.Read(b->new_class_,&replacement)||
     !classes.emplace(old,false).second||!classes.emplace(replacement,true).second)
   return deny("call-graph-replacement-batch-Class-alias-or-fresh-binding-refused");
  for(const auto&m:b->declared_)if(!native.emplace(m.method,true).second)
   return deny("call-graph-replacement-batch-native-method-alias-refused");
  for(const auto&copy:b->copied_)if(!native.emplace(copy.native.method,true).second)
   return deny("call-graph-replacement-batch-copied-method-alias-refused");
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
  classes_.push_back({b.new_class_,b.parent_,b.interfaces_,b.incoming_->Schema()->access});
  const size_t base=nodes_.size();
  std::unordered_map<uint32_t,size_t>indices;
  for(size_t n=0;n<b.declared_.size();++n){const auto&m=b.declared_[n];
   const size_t ordinal=nodes_.size();
   if(!indices.emplace(m.index,n).second)return deny("call-graph-replacement-outgoing-index-ambiguous");
   nodes_.push_back({{dex.begin,token,m.method},(jvmti_method_flags::Public(m.flags)&0x100u)?native_call_graph::MethodKind::Native:native_call_graph::MethodKind::Managed});
   node_bindings_.push_back({i,n});
   Source body;body.incoming=b.incoming_;body.index=m.index;
   const auto&input=b.incoming_->Methods()[n];
   body.calls=input.calls;
   if(m.body){
    if(!body.incoming->Code(body.index,&body.code)||body.code.units>SIZE_MAX/2||
       input.instruction_bytes.size()!=body.code.units*2)
     return deny("call-graph-replacement-declaration-source-body-unavailable");
    body.bytes=input.instruction_bytes;
   }else if(!body.calls.empty()||!input.instruction_bytes.empty())
    return deny("call-graph-replacement-bodyless-declaration-has-code");
   sources_.push_back(std::move(body));
   graph_methods_.push_back({m.method,b.new_class_,b.new_class_,m.index,jvmti_method_flags::Public(m.flags)});
   if(m.body)seeds.push_back(ordinal);
  }
  // Incoming facts and native declarations have the same independently
  // checked order/index. DEX indices never join across namespaces or loaders.
  const auto&methods=b.incoming_->Methods();
  for(size_t n=0;n<methods.size();++n)for(const auto&call:sources_[base+n].calls){
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
  for(size_t n=0;n<b.copied_.size();++n){const auto&copy=b.copied_[n];
   const auto&m=copy.native;const auto*declaration=copy.Declaration();const auto loader_root=copy.Loader();
   void*declaring=nullptr;void*copy_loader=nullptr;
   evolution::MappedDexMetadata source_dex{};field_copy::CopyClassView source_name{};
   if(!declaration||!roots_->Read(declaration->declaring_root,&declaring)||
      !roots_->Read(loader_root,&copy_loader)||
      !native_interface_dispatch::ReadCurrentNamespace(*b.metadata_,declaring,read,&source_dex,&source_name)||
      source_name.loader!=copy_loader)return deny("call-graph-replacement-copied-declaration-namespace-refused");
   uint64_t copy_token=0;
   if(copy_loader){size_t found=0;
    for(;found<loaders_.size();++found){void*other=nullptr;
     if(!roots_->Read(loaders_[found],&other))return deny("call-graph-replacement-copied-loader-root-refused");
     if(other==copy_loader)break;
    }
    if(found==loaders_.size())loaders_.push_back(loader_root);
    copy_token=found+1;
   }
   const size_t ordinal=nodes_.size();
   nodes_.push_back({{source_dex.begin,copy_token,m.method},native_call_graph::MethodKind::Managed});
   node_bindings_.push_back({i,n,true});
   graph_methods_.push_back({m.method,declaration->declaring_root,b.new_class_,m.index,
     jvmti_method_flags::Public(m.flags)});
   Source body;body.incoming=copy.incoming_owner?copy.incoming_owner->incoming_:nullptr;
   body.before=copy.incoming_owner?nullptr:b.before_;body.index=m.index;
   if(m.body){
    if(copy.incoming_owner){
     const auto&input=copy.incoming_owner->incoming_->Methods()[copy.incoming_ordinal];
     if(!body.incoming->Code(m.index,&body.code)||body.code.units>SIZE_MAX/2||
        input.instruction_bytes.size()!=body.code.units*2)return deny("call-graph-replacement-copied-incoming-body-absent");
     body.bytes=input.instruction_bytes;body.calls=input.calls;
    }else{
     const auto&source=copy.source;
     if(!source.code||!source.bytes||!source.calls)return deny("call-graph-replacement-copied-source-body-absent");
     body.code=*source.code;body.bytes=*source.bytes;
     for(const auto&site:*source.calls){native_call_graph_incoming::Call call;call.site=site;
      if(!evolution::ReadMappedDexMethodReference(body.code.source_dex,site.instruction.index,&call.reference,error))return false;
      body.calls.push_back(std::move(call));
     }
    }
    seeds.push_back(ordinal);
   }
   for(const auto&call:body.calls)edges_.push_back({ordinal,SIZE_MAX,call.site.dex_pc,
     native_call_graph::Kind(call.site.instruction.kind),native_call_graph::Coverage::Unresolved});
   sources_.push_back(std::move(body));
  }
 }
 plan_=native_call_graph::Build(nodes_,edges_,seeds);
 return plan_.failure==native_call_graph::Failure::None||deny("call-graph-replacement-outgoing-graph-invalid");
}
bool Batch::Source::Matches(receiver_guard::CodeView&actual)const noexcept{
 if((bool(incoming)==bool(before))||index==UINT32_MAX||!code.code_item||code.units>SIZE_MAX/2||
    bytes.size()!=code.units*2||!actual.source_dex.begin)return false;
 actual.source_dex.class_def_index=code.source_dex.class_def_index;
 if(incoming&&!incoming->MatchesCode(index,actual))return false;
 if(before&&(actual.source_dex.begin!=code.source_dex.begin||actual.source_dex.data_begin!=code.source_dex.data_begin||
    actual.source_dex.size!=code.source_dex.size||actual.source_dex.data_size!=code.source_dex.data_size||
    actual.code_item!=code.code_item||actual.instructions!=code.instructions||actual.units!=code.units||
    actual.registers!=code.registers||actual.ins_size!=code.ins_size||actual.tries_size!=code.tries_size||
    actual.method_ids!=code.method_ids||actual.call_sites!=code.call_sites||actual.compact!=code.compact))return false;
 return !std::memcmp(bytes.data(),actual.instructions,bytes.size());
}
const Binding::Declared*Batch::Carrier(const NodeBinding&bound)const noexcept{
 if(bound.binding>=bindings_.size())return nullptr;const auto&b=*bindings_[bound.binding];
 if(bound.copied)return bound.declared<b.copied_.size()?&b.copied_[bound.declared].native:nullptr;
 return bound.declared<b.declared_.size()?&b.declared_[bound.declared]:nullptr;
}
bool Batch::ResolveCalls(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,std::string*error,
 const native_call_graph_inventory::Inventory*before,const native_call_graph_super::Resolver*super){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!prepared_||resolution_attempted_||bindings_.empty()||sources_.size()!=node_bindings_.size()||!context.env||
    context.self!=bindings_.front()->self_||!runnable_jni::Bridge::BorrowOriginalScope(context))
  return deny("call-graph-replacement-resolution-original-scope-refused");
 resolution_attempted_=true;super_resolver_=super;
 std::vector<native_call_graph_resolution::Result>candidate;
 candidate.reserve(edges_.size());
 for(size_t ordinal=0;ordinal<node_bindings_.size();++ordinal){
  const auto&bound=node_bindings_[ordinal];const auto&b=*bindings_[bound.binding];
  const auto*carrier=Carrier(bound);
  if(!carrier)return deny("call-graph-replacement-resolution-carrier-unavailable");
  const auto&m=sources_[ordinal];
  if(b.source_!=&source)return deny("call-graph-replacement-resolution-source-owner-differs");
  if(m.calls.empty())continue;
  if(!m.code.code_item||m.code.units>SIZE_MAX/2)
   return deny("call-graph-replacement-resolution-incoming-code-unavailable");
  if(m.bytes.size()!=m.code.units*2)return deny("call-graph-replacement-resolution-instruction-bytes-unavailable");
  const auto&bytes=m.bytes;
  for(size_t site=0;site<m.calls.size();++site){const auto&call=m.calls[site];
   receiver_guard::CodeView actual{};
   {
    // Original resolution may suspend. Recreate the native mapping snapshot
    // and decode the pinned Class/Holder again before every individual call.
    method_identity::ScopedReadableMappings mappings;
    if(!mappings.Ready())return deny("call-graph-replacement-resolution-mappings-unavailable");
    modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
    if(!b.Check(read)||!CheckGraph(read)||!source.Code(context.self,carrier->method,&actual,read))
     return deny("call-graph-replacement-resolution-current-rooted-body-refused");
    if(!m.Matches(actual))
     return deny("call-graph-replacement-resolution-incoming-body-differs");
   }
   auto result=native_call_graph_resolution::ResolveOriginal(context,source,
    carrier->method,actual,bytes,call.site);
   if(result.status==native_call_graph_resolution::Status::OriginalException)
    return deny("call-graph-replacement-original-resolution-exception-pending");
   if(before&&super&&call.site.instruction.kind==receiver_guard::InvokeKind::Super&&
      result.status==native_call_graph_resolution::Status::Resolved){
    method_identity::ScopedReadableMappings mappings;
    if(!mappings.Ready())return deny("call-graph-replacement-super-mappings-unavailable");
    modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
    native_call_graph_super::Observation observed{};
    if(!b.Check(read)||!super->ResolveIncoming(context,source,*b.loading_,b.driver_,b.holder_,b.owner_count_,
       carrier->method,actual,bytes,call.site,result.declaration,read,&observed,error)||
       !CaptureSuper(*before,ordinal,site,observed,error))return false;
   }
   candidate.push_back(result);
  }
 }
 // No partial receipt publication, including exceptions and changed Holders.
 method_identity::ScopedReadableMappings mappings;
 if(!mappings.Ready())return deny("call-graph-replacement-resolution-final-mappings-unavailable");
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 for(const auto&b:bindings_)if(!b->Check(read))return deny("call-graph-replacement-resolution-final-binding-refused");
 if(!CheckGraph(read)||!CheckSupers(&context,nullptr,read)||candidate.size()!=edges_.size())
  return deny("call-graph-replacement-resolution-final-graph-refused");
 resolutions_=std::move(candidate);resolved_=true;if(error)error->clear();return true;
}
bool Batch::CaptureSuper(const native_call_graph_inventory::Inventory&before,size_t caller,size_t site,
 const native_call_graph_super::Observation&observed,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(caller>=node_bindings_.size()||caller>=sources_.size()||!before.RootStore())return deny("call-graph-replacement-super-root-owner-unavailable");
 const auto&bound=node_bindings_[caller];const auto&b=*bindings_[bound.binding];
 const auto*carrier=Carrier(bound);
 const auto&m=sources_[caller];
 if(!carrier||site>=m.calls.size()||observed.caller!=carrier->method||
    observed.dex_pc!=m.calls[site].site.dex_pc)return deny("call-graph-replacement-super-caller-receipt-differs");
 // No JNI or resolver between the original return and this root join. These
 // slots were independently staged before resolution; descriptors grant none.
 Super witness;witness.caller=caller;witness.site=site;
 size_t references=0;bool readable=true;
 const auto find_class=[&](const auto&inventory){const auto*store=inventory.RootStore();
  if(!store){readable=false;return;}
  for(const auto&klass:inventory.Classes()){void*current=nullptr;
   if(!store->Read(klass.root,&current)){readable=false;return;}
   if(current==observed.referenced_class){++references;witness.referenced={store,store->Generation(),klass.root};}
  }
 };
 find_class(before);find_class(*this);
 if(!readable||references!=1)return deny("call-graph-replacement-super-reference-outside-rooted-union");
 const auto find_method=[&](void*method,Root*declaring,Root*containing){size_t found=0;
  const auto search=[&](const auto&inventory){const auto*store=inventory.RootStore();
   for(const auto&member:inventory.Declarations())if(member.method==method){
    ++found;*declaring={store,store->Generation(),member.declaring_root};
    *containing={store,store->Generation(),member.containing_root};
   }
  };
  search(before);search(*this);return found==1;
 };
 if(!find_method(observed.declaration,&witness.declaration_declaring,&witness.declaration_containing)||
    !find_method(observed.target,&witness.target_declaring,&witness.target_containing))
  return deny("call-graph-replacement-super-method-outside-rooted-union");
 void*klass=nullptr;void*parent=nullptr;
 if(caller>=graph_methods_.size()||!roots_->Read(graph_methods_[caller].declaring_root,&klass)||klass!=observed.caller_class)
  return deny("call-graph-replacement-super-caller-root-changed");
 if(!observed.interface_super){
  if(bound.copied||!roots_->Read(b.parent_,&parent))return deny("call-graph-replacement-copied-class-super-owner-unproved");
  if(parent!=observed.super_class)return deny("call-graph-replacement-super-parent-root-changed");
 }
 if(!m.code.code_item||
    !evolution::ReadMappedDexMethodReference(m.code.source_dex,m.calls[site].site.instruction.index,&witness.reference,error)||
    witness.reference.class_index!=observed.type_index)return deny("call-graph-replacement-super-input-reference-differs");
 witness.declaration=observed.declaration;witness.target=observed.target;
 witness.type=observed.type_index;witness.slot=observed.slot;
 witness.interface_super=observed.interface_super;
 supers_.push_back(std::move(witness));return true;
}
void*Batch::SuperTarget(void*caller,uint32_t pc)const noexcept{
 if(!resolved_)return nullptr;
 for(const auto&w:supers_){const auto&bound=node_bindings_[w.caller];
  const auto*carrier=Carrier(bound);
  if(w.caller<sources_.size()&&w.site<sources_[w.caller].calls.size()&&
     carrier&&carrier->method==caller&&sources_[w.caller].calls[w.site].site.dex_pc==pc)return w.target;
 }
 return nullptr;
}
bool Batch::ValidateSupers(const runnable_jni::Context&context,const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&CheckSupers(&context,nullptr,read);
}
bool Batch::CheckSupers(const runnable_jni::Context*context,const native_source_stop::Session*stop,
 const modern_field_metadata::ReadContext&read)const noexcept{
 if(supers_.empty())return true;
 if(!super_resolver_||(!context&&!stop))return false;
 const auto root=[](const Root&r,void**value){return r.store&&r.store->Generation()==r.generation&&r.store->Read(r.slot,value)&&*value;};
 for(const auto&w:supers_){
  if(w.caller>=node_bindings_.size()||w.caller>=sources_.size())return false;
  const auto&bound=node_bindings_[w.caller];const auto&b=*bindings_[bound.binding];
  const auto*carrier=Carrier(bound);
  const auto&m=sources_[w.caller];
  if(!carrier||(!w.interface_super&&bound.copied)||w.site>=m.calls.size()||w.caller>=graph_methods_.size())return false;
  void*referenced=nullptr;void*klass=nullptr;void*parent=nullptr;
  if(!root(w.referenced,&referenced)||!roots_->Read(graph_methods_[w.caller].declaring_root,&klass)||!klass||
     (!w.interface_super&&(!roots_->Read(b.parent_,&parent)||!parent)))return false;
  const auto member=[&](void*method,const Root&declaring_root,const Root&containing_root){
   void*declaring=nullptr;void*containing=nullptr;
   if(!root(declaring_root,&declaring)||!root(containing_root,&containing)||
      !Mapped(read,method,b.layout_.size)||b.metadata_->MethodDeclaringClass(method)!=declaring||!Mapped(read,containing,56))return false;
   auto*array=reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(containing,48)));
   size_t extent=0;
   return array&&read.extent&&read.extent(read.owner,array,&extent)&&
          native_selective_interpretation::detail::Member(array,extent,method,b.layout_.size);
  };
  if(!member(w.declaration,w.declaration_declaring,w.declaration_containing)||
     !member(w.target,w.target_declaring,w.target_containing))return false;
  receiver_guard::CodeView actual{};
  if(!b.source_->Code(b.self_,carrier->method,&actual,read)||
     !m.Matches(actual))return false;
  native_call_graph_super::Observation observed{carrier->method,referenced,klass,parent,
   w.declaration,w.target,actual.source_dex.begin,m.calls[w.site].site.dex_pc,w.type,w.slot,w.interface_super};
  if(stop){if(!super_resolver_->ValidateObserved(*stop,b.self_,actual,m.bytes,m.calls[w.site].site,w.reference,observed,read))return false;}
  else if(context->self!=b.self_||!super_resolver_->ValidateObservedOrdinary(*context,*b.source_,actual,m.bytes,
            m.calls[w.site].site,w.reference,observed,read))return false;
 }
 return true;
}
bool Batch::CheckGraph(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!roots_||roots_->Generation()!=generation_||nodes_.size()!=node_bindings_.size()||
    sources_.size()!=nodes_.size()||graph_methods_.size()!=nodes_.size())return false;
 for(size_t i=0;i<loaders_.size();++i){void*loader=nullptr;
  if(!roots_->Read(loaders_[i],&loader)||!loader)return false;
  for(size_t j=0;j<i;++j){void*other=nullptr;if(!roots_->Read(loaders_[j],&other)||other==loader)return false;}
 }
 for(size_t i=0;i<nodes_.size();++i){const auto&binding=node_bindings_[i];
  if(binding.binding>=bindings_.size())return false;const auto&b=*bindings_[binding.binding];
  const auto*method=Carrier(binding);if(!method)return false;
  const auto&body=sources_[i];
  const auto*incoming=binding.copied?(b.copied_[binding.declared].incoming_owner?
    b.copied_[binding.declared].incoming_owner->incoming_:nullptr):b.incoming_;
  const auto*before=incoming?nullptr:b.before_;
  if(body.index!=method->index||body.incoming!=incoming||body.before!=before)return false;
  if(method->body){receiver_guard::CodeView current{};
   if(!b.source_->Code(b.self_,method->method,&current,read)||!body.Matches(current))return false;
  }else if(body.code.code_item||!body.calls.empty()||!body.bytes.empty())return false;
  const auto&node=nodes_[i];void*klass=nullptr;void*loader=nullptr;evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  const auto loader_root=binding.copied?b.copied_[binding.declared].Loader():b.loader_;
  if(!roots_->Read(graph_methods_[i].declaring_root,&klass)||!roots_->Read(loader_root,&loader)||
     !native_interface_dispatch::ReadCurrentNamespace(*b.metadata_,klass,read,&dex,&named)||
     named.loader!=loader||node.identity.dex_namespace!=dex.begin||node.identity.native_method!=method->method)return false;
  const auto token=node.identity.loader_token;
  if(!token){if(loader)return false;}
  else {void*representative=nullptr;
   if(token>loaders_.size()||!roots_->Read(loaders_[token-1],&representative)||representative!=loader)return false;
  }
 }
 return true;
}
bool Batch::ValidateOrdinary(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!prepared_||!roots_||roots_->Generation()!=generation_||bindings_.empty())return false;
 for(const auto&b:bindings_)if(!b->Check(read))return false;
 return CheckGraph(read);
}
bool Batch::OwnsSource(const native_source_class_loading::Run&loading,const void*driver,const void*holder)const noexcept{
 if(!prepared_||!roots_||roots_->Generation()!=generation_||bindings_.empty()||
    native_source_class_loading::Run::Current()!=&loading||!loading.OwnsDriver(driver))return false;
 for(const auto&binding:bindings_)
  if(binding->loading_!=&loading||binding->driver_!=driver||binding->holder_!=holder||
     !loading.OwnsFinishedHolder(holder,binding->owner_count_))return false;
 return true;
}
bool Batch::Validate(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateFor(stop,read,Binding::Purpose::Forward);
}
bool Batch::ValidateRefund(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateFor(stop,read,Binding::Purpose::Refund);
}
bool Batch::ValidateFor(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read,Binding::Purpose purpose)const noexcept{
 if(!prepared_||!roots_||roots_->Generation()!=generation_)return false;
 for(size_t i=0;i<bindings_.size();++i){const auto&b=*bindings_[i];
  if(!b.ValidateFor(stop,read,purpose))return false;
  void*old=nullptr;void*replacement=nullptr;
  if(!roots_->Read(b.old_class_,&old)||!roots_->Read(b.new_class_,&replacement))return false;
  for(size_t j=0;j<i;++j){void*other_old=nullptr;void*other_new=nullptr;
   if(!roots_->Read(bindings_[j]->old_class_,&other_old)||!roots_->Read(bindings_[j]->new_class_,&other_new)||
      old==other_old||old==other_new||replacement==other_old||replacement==other_new)return false;
  }
 }
 return CheckGraph(read)&&CheckSupers(nullptr,&stop,read)&&stop.IsStopped(bindings_.front()->self_);
}
}

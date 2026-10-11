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
 modern_field_metadata::ReadContext read{&mappings,[](void*p,const void*address,size_t*extent){return static_cast<method_identity::ScopedReadableMappings*>(p)->Extent(address,extent);}};
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
 if(!roots_||roots_->Generation()!=generation_||!loading_||!loading_->OwnsDriver(driver_)||
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
}

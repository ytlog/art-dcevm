#include "platform/common/native_receiver_class_source.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/native_receiver_dex_view.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include <cstring>
namespace artdcevm::receiver_guard {
namespace {
const class_metadata::Layout&Layout(const modern_field_metadata::FieldMetadata&m){return m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;}
bool Mapped(const void*p,size_t bytes,const modern_field_metadata::ReadContext*context=nullptr){size_t extent=0;return p&&modern_field_metadata::ReadMappingExtent(context,p,&extent)&&extent>=bytes;}
}
bool ClassSource::Bind(HookEngine&hooks,const modern_field_metadata::FieldMetadata&metadata,void*(*current)(),std::string*error,void*linker){
 if(bound_||!metadata.Calibrated()||!current){if(error)*error="Receiver Class source lacks actual calibrated metadata/current Thread";return false;}
 auto obsolete=reinterpret_cast<abi::arm64::android_common::ObsoleteClass>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kObsoleteClass));
 auto direct=reinterpret_cast<abi::arm64::android_common::DirectInterface>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kDirectInterface));
 auto type=reinterpret_cast<abi::arm64::android_common::DirectInterfaceType>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kDirectInterfaceType));
 auto lookup=reinterpret_cast<abi::arm64::android_common::LookupDirectInterface>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kLookupDirectInterface));
 if(!obsolete||(!direct&&(!linker||!type||!lookup))){if(error)*error="Named original Class history/direct interface source unavailable";return false;}
 metadata_=&metadata;current_=current;obsolete_=obsolete;direct_=direct;direct_type_=type;lookup_direct_=lookup;linker_=linker;interfaces_calibrated_=metadata.InterfaceDeclarationCalibrated();bound_=true;return true;
}
bool ClassSource::CalibrateInterfaces(const runnable_jni::Context&context,jobject fixture,std::string*error,const native_interface_graph::StrongRoots*decoder){
 if(!Current()||context.self!=current_()||!fixture)return false;
 if(interfaces_calibrated_&&!decoder)return true;
 if(!native_interface_graph::Calibrate(context,fixture,*metadata_,error,decoder)||!Current()||context.self!=current_())return false;
 interfaces_calibrated_=true;return true;
}
bool ClassSource::ProxyInterfaces(void*klass,void**array,size_t*count,const modern_field_metadata::ReadContext*ctx)const{
 field_array::View fields;if(!array||!count||!metadata_->Fields(klass,true,&fields,nullptr,ctx)||fields.selected_count!=2)return false;
 void*interfaces=nullptr;
 for(size_t i=0;i<fields.selected_count;++i){auto*field=static_cast<const field_array::Field*>(fields.AtSelected(i));field_copy::CopyFieldView identity;
  if(!field||!metadata_->FieldIdentity(const_cast<field_array::Field*>(field),&identity,nullptr,ctx)||metadata_->FieldDeclaringClass(const_cast<field_array::Field*>(field))!=klass)return false;
  if(field->dex_field_index==0){if(!(field_array::Flags(field)&8u)||std::strcmp(identity.name,"interfaces")||std::strcmp(identity.descriptor,"[Ljava/lang/Class;")||!Mapped(klass,size_t(field->offset)+4,ctx))return false;interfaces=metadata_->Reference(klass,field->offset);}
 }
 // Fixed Class::GetProxyInterfaces static interfaces field and Class[] source.
 if(!Mapped(interfaces,12,ctx))return false;void*meta=metadata_->Reference(klass,0),*array_class=metadata_->Reference(interfaces,0);
 if(!Mapped(array_class,Layout(*metadata_).size,ctx)||metadata_->Reference(array_class,0)!=meta||metadata_->Reference(array_class,offsetof(class_metadata::Class11,component_type))!=meta)return false;
 int32_t length=0;std::memcpy(&length,static_cast<char*>(interfaces)+8,4);
 if(length<0||!Mapped(interfaces,12+size_t(length)*4,ctx))return false;*array=interfaces;*count=size_t(length);return true;
}
bool ClassSource::Definition(void*klass,evolution::MappedDexMetadata*out,evolution::MappedDexClassIdentity*identity,const modern_field_metadata::ReadContext*ctx)const{
 if(!ctx)return metadata_->DefiningClassMapping(klass,out,identity,nullptr);
 if(!out||!identity)return false;size_t extent=0;class_metadata::Identity declaration;
 if(!modern_field_metadata::ReadMappingExtent(ctx,klass,&extent)||!class_metadata::ReadIdentity(Layout(*metadata_),klass,extent,&declaration)||declaration.class_def_index<0||declaration.dex_type_index<0)return false;
 void*cache=metadata_->Reference(klass,offsetof(class_metadata::Class11,dex_cache));
 constexpr size_t offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
 if(!Mapped(cache,offset+8,ctx))return false;uint64_t bits=0;std::memcpy(&bits,static_cast<char*>(cache)+offset,8);auto*file=reinterpret_cast<void*>(uintptr_t(bits));
 if(!Mapped(file,sizeof(dex_mapping_abi::Prefix),ctx))return false;dex_mapping_abi::Prefix prefix;std::memcpy(&prefix,file,sizeof(prefix));
 if(!Mapped(prefix.begin,112,ctx)||!modern_field_metadata::ReadMappingExtent(ctx,prefix.data_begin,&extent)||prefix.data_size>extent)return false;
 uint32_t size=0;std::memcpy(&size,prefix.begin+32,4);if(size<112||!Mapped(prefix.begin,size,ctx))return false;
 evolution::MappedDexMetadata map{prefix.begin,size,prefix.data_begin,prefix.data_size,uint32_t(declaration.class_def_index)};BoundedDexView dex(map);const char*descriptor=nullptr;uint32_t flags=0,type=0;
 if(!dex.Initialize()||!dex.Identity(&descriptor,&flags,&type)||type!=uint32_t(declaration.dex_type_index))return false;
 *out=map;*identity={descriptor,flags};return true;
}
bool ClassSource::ReadIn(void*klass,ClassView*out,const modern_field_metadata::ReadContext*ctx)const noexcept{
 const auto&s=*this;if(!out||!s.Current()||!s.interfaces_calibrated_||!Mapped(klass,Layout(*s.metadata_).size,ctx))return false;
 try{size_t extent=0;class_metadata::State state;if(!modern_field_metadata::ReadMappingExtent(ctx,klass,&extent)||!class_metadata::ReadState(Layout(*s.metadata_),klass,extent,&state)||state.IsPrimitive())return false;
  void*meta=s.metadata_->Reference(klass,0);if(!Mapped(meta,Layout(*s.metadata_).size,ctx)||s.metadata_->Reference(meta,0)!=meta)return false;
  native_interface_graph::View interfaces;if(!native_interface_graph::ReadView(*s.metadata_,klass,&interfaces,nullptr,ctx))return false;
  ClassView view;view.flags=state.access_flags;view.parent=s.metadata_->Reference(klass,offsetof(class_metadata::Class11,super_class));view.loader=s.metadata_->Reference(klass,offsetof(class_metadata::Class11,class_loader));view.interface_count=interfaces.Count();
  if(s.metadata_->Reference(klass,offsetof(class_metadata::Class11,component_type))){if(interfaces.Count()!=2)return false;view.kind=ClassKind::Array;view.direct_count=2;}
  else if(state.access_flags&0x40000u){void*array=nullptr;if(!s.ProxyInterfaces(klass,&array,&view.direct_count,ctx))return false;view.kind=ClassKind::Proxy;}
  else{field_copy::CopyClassView identity;evolution::MappedDexMetadata map;evolution::MappedDexClassIdentity definition;evolution::MappedDexDirectInterfaces direct;std::string error;
   if(!s.Definition(klass,&map,&definition,ctx)||!s.metadata_->ClassIdentity(klass,&identity,nullptr,ctx)||!identity.descriptor||std::strcmp(identity.descriptor,definition.descriptor))return false;
   if(ctx){BoundedDexView dex(map);if(!dex.Initialize()||!dex.DirectCount(&direct.count))return false;}
   else if(!evolution::ReadMappedDexDirectInterfaces(map,&direct,&error))return false;
   if((state.access_flags&0x7631u)!=(definition.access&0x7631u))return false;view.descriptor=definition.descriptor;view.direct_count=direct.count;
  }if(!s.Current())return false;*out=view;return true;
 }catch(...){return false;}
}
bool ClassSource::InterfaceIn(void*klass,size_t index,void**out,const modern_field_metadata::ReadContext*ctx)const noexcept{const auto&s=*this;if(!s.Current()||!out)return false;native_interface_graph::View view;return native_interface_graph::ReadView(*s.metadata_,klass,&view,nullptr,ctx)&&native_interface_graph::InterfaceAt(*s.metadata_,view,index,out,ctx)&&s.Current();}
bool ClassSource::DirectIn(void*klass,size_t index,void**out,const modern_field_metadata::ReadContext*ctx)const noexcept{
 const auto&s=*this;ClassView owner;if(!out||index>UINT32_MAX||!s.ReadIn(klass,&owner,ctx)||index>=owner.direct_count)return false;
 // GetDirectInterface uses LookupResolvedType, never ResolveDirectInterface's
 // allocating/waiting fallback. Exact pointer membership and DEX descriptor
 // independently close the original leaf's resolved return.
 void*value=nullptr;
 if(owner.kind==ClassKind::Array){native_interface_graph::View table;if(!native_interface_graph::ReadView(*s.metadata_,klass,&table,nullptr,ctx)||!native_interface_graph::InterfaceAt(*s.metadata_,table,index,&value,ctx))return false;}
 else if(owner.kind==ClassKind::Proxy){void*array=nullptr;size_t count=0;if(!s.ProxyInterfaces(klass,&array,&count,ctx)||index>=count)return false;value=s.metadata_->Reference(array,12+index*4);}
 else if(s.direct_)value=reinterpret_cast<void*>(s.direct_(klass,uint32_t(index)).reference);
 else {const auto type=s.direct_type_(klass,uint32_t(index));value=reinterpret_cast<void*>(s.lookup_direct_(s.linker_,type,{uintptr_t(klass)}).reference);}
 ClassView identity;if(!value||!s.ReadIn(value,&identity,ctx)||!(identity.flags&0x200u)||identity.kind!=ClassKind::Ordinary)return false;
 native_interface_graph::View interfaces;if(!native_interface_graph::ReadView(*s.metadata_,klass,&interfaces,nullptr,ctx))return false;size_t matches=0;
 for(size_t i=0;i<interfaces.Count();++i){void*iface=nullptr;if(!native_interface_graph::InterfaceAt(*s.metadata_,interfaces,i,&iface,ctx))return false;if(iface==value)++matches;}if(matches!=1)return false;
 try{if(owner.kind==ClassKind::Ordinary){evolution::MappedDexMetadata map;evolution::MappedDexClassIdentity definition;const char*expected=nullptr;std::string error;
   if(!s.Definition(klass,&map,&definition,ctx)||!(ctx?([&]()noexcept{BoundedDexView dex(map);return dex.Initialize()&&dex.DirectAt(uint32_t(index),&expected);}()):evolution::ReadMappedDexDirectInterfaceAt(map,uint32_t(index),&expected,&error))||std::strcmp(expected,identity.descriptor))return false;
  }else if(owner.kind==ClassKind::Proxy){void*array=nullptr;size_t count=0;if(!s.ProxyInterfaces(klass,&array,&count,ctx)||index>=count||s.metadata_->Reference(array,12+index*4)!=value)return false;
  }else{void*expected=nullptr;if(!native_interface_graph::InterfaceAt(*s.metadata_,interfaces,index,&expected,ctx)||expected!=value)return false;}
 }catch(...){return false;}if(!s.Current())return false;*out=value;return true;
}
bool ClassSource::ObsoleteIn(void*klass,void**out,const modern_field_metadata::ReadContext*ctx)const noexcept{const auto&s=*this;ClassView view;if(!out||!s.ReadIn(klass,&view,ctx))return false;void*old=reinterpret_cast<void*>(s.obsolete_(klass).reference);if(old){ClassView identity;if(view.kind!=ClassKind::Ordinary||!s.ReadIn(old,&identity,ctx)||identity.kind!=ClassKind::Ordinary)return false;}if(!s.Current())return false;*out=old;return true;}
bool ClassSource::Read(void*raw,void*k,ClassView*out)noexcept{return static_cast<ClassSource*>(raw)->ReadIn(k,out,nullptr);}
bool ClassSource::InterfaceAt(void*raw,void*k,size_t i,void**out)noexcept{return static_cast<ClassSource*>(raw)->InterfaceIn(k,i,out,nullptr);}
bool ClassSource::DirectAt(void*raw,void*k,size_t i,void**out)noexcept{return static_cast<ClassSource*>(raw)->DirectIn(k,i,out,nullptr);}
bool ClassSource::Obsolete(void*raw,void*k,void**out)noexcept{return static_cast<ClassSource*>(raw)->ObsoleteIn(k,out,nullptr);}
ClassSource::ReadWindow::ReadWindow(const ClassSource&s,const modern_field_metadata::ReadContext&c)noexcept:source_(s),context_(c),owner_(c.owner),extent_(c.extent),self_(s.Current()?s.current_():nullptr){}
bool ClassSource::ReadWindow::Current()const noexcept{return owner_&&extent_&&context_.owner==owner_&&context_.extent==extent_&&self_&&source_.Current()&&source_.current_()==self_;}
bool ClassSource::ReadWindow::Read(void*raw,void*k,ClassView*out)noexcept{const auto&s=*static_cast<ReadWindow*>(raw);return s.Current()&&s.source_.ReadIn(k,out,&s.context_)&&s.Current();}
bool ClassSource::ReadWindow::InterfaceAt(void*raw,void*k,size_t i,void**out)noexcept{const auto&s=*static_cast<ReadWindow*>(raw);return s.Current()&&s.source_.InterfaceIn(k,i,out,&s.context_)&&s.Current();}
bool ClassSource::ReadWindow::DirectAt(void*raw,void*k,size_t i,void**out)noexcept{const auto&s=*static_cast<ReadWindow*>(raw);return s.Current()&&s.source_.DirectIn(k,i,out,&s.context_)&&s.Current();}
bool ClassSource::ReadWindow::Obsolete(void*raw,void*k,void**out)noexcept{const auto&s=*static_cast<ReadWindow*>(raw);return s.Current()&&s.source_.ObsoleteIn(k,out,&s.context_)&&s.Current();}
ClassAccess ClassSource::ReadWindow::Access()noexcept{return {this,Read,InterfaceAt,DirectAt,Obsolete};}
ClassAccess ClassSource::OrdinaryAccess()noexcept{return {this,Read,InterfaceAt,DirectAt,Obsolete};}
bool ClassSource::Observe(const runnable_jni::Context&context,jobject root,jobject parent,jobject loader,const std::vector<jobject>&direct,bool public_interface,void*expected_owner)noexcept{
 if(!Current()||context.self!=current_()||!context.env||context.env->ExceptionCheck()||!root||!expected_owner)return false;
 void*klass=nullptr,*native_parent=nullptr,*native_loader=nullptr;
 if(!runnable_executor::DecodeStrongGlobal(context,root,&klass)||klass!=expected_owner||(parent&&!runnable_executor::DecodeStrongGlobal(context,parent,&native_parent))||(loader&&!runnable_executor::DecodeStrongGlobal(context,loader,&native_loader)))return false;
 ClassView view;if(!Read(this,klass,&view)||view.kind!=ClassKind::Ordinary||bool(view.flags&0x200)!=public_interface||view.parent!=native_parent||view.loader!=native_loader||view.direct_count!=direct.size())return false;
 for(size_t i=0;i<direct.size();++i){void*expected=nullptr,*actual=nullptr;if(!direct[i]||!runnable_executor::DecodeStrongGlobal(context,direct[i],&expected)||!DirectAt(this,klass,i,&actual)||actual!=expected)return false;}
 void*old=nullptr;return Obsolete(this,klass,&old)&&!old&&Current()&&context.self==current_();
}

}

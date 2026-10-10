#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_receiver_code_item.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include "platform/common/class_metadata_abi.h"
#include <cstring>
namespace artdcevm::receiver_guard {
namespace {
struct Resolution {const SourceProducer*owner=nullptr;void*self=nullptr;bool original_null=false;};
thread_local Resolution resolution;
bool Extent(const void*p,size_t*n)noexcept{return modern_field_metadata::ReadMappingExtent(nullptr,p,n);}
uint32_t U32(uintptr_t p)noexcept{uint32_t n;std::memcpy(&n,reinterpret_cast<const void*>(p),4);return n;}
}
bool SourceProducer::Bind(HookEngine&hooks,stack_visitor_service::Declaration declaration,const code_item_metadata::Layout&layout,const modern_field_metadata::FieldMetadata&metadata,void*linker,void*(*current)(),std::string*error){
 if(bound_||!metadata.Calibrated()||!linker||!current){if(error)*error="Receiver producer needs exact calibrated metadata/current Thread/ClassLinker";return false;}
 stack_visitor_service::Service stack;auto finder=[&](const char*n){return hooks.FindSymbol("libart.so",n);};
 // Fixed runtime/class_linker.h: ResolveMode::kCheckICCEAndIAE=1, exact
 // Thread/referrer overload. Its null return has the original Java exception.
 auto resolve=reinterpret_cast<ResolveMethod>(finder("_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE"));
 if(!resolve||!stack_visitor_service::Service::Bind(declaration,finder,&stack)){if(error)*error="Named checked ResolveMethod or calibrated StackVisitor unavailable";return false;}
 obsolete_cache_=reinterpret_cast<void*(*)(void*)>(finder("_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE0EEENS_6ObjPtrINS_6mirror8DexCacheEEEv"));
 stack_=stack;declaration_=declaration;layout_=layout;metadata_=&metadata;linker_=linker;current_=current;resolve_=resolve;bound_=true;return true;
}
bool SourceProducer::Current(void*p,void*self)noexcept{auto&s=*static_cast<SourceProducer*>(p);return s.bound_&&self&&s.current_&&s.current_()==self;}
bool SourceProducer::Code(void*self,void*method,CodeView*out)const noexcept{
 if(!out||!Current(const_cast<SourceProducer*>(this),self)||!method)return false;size_t extent=0;method_metadata::Identity identity;
 if(!Extent(method,&extent)||!method_metadata::ReadIdentity(layout_.method,method,extent,&identity)||identity.dex_method_index==UINT32_MAX||(identity.access_flags&0x500u))return false;
 void*klass=metadata_->Reference(method,0);
 // Fixed ArtMethod::GetDexCache selects the retained obsolete cache instead
 // of a restored declaring Class whose current DEX can already be different.
 void*cache=nullptr;if(identity.access_flags&0x40000u){if(!obsolete_cache_)return false;cache=obsolete_cache_(method);}else cache=klass?metadata_->Reference(klass,offsetof(class_metadata::Class11,dex_cache)):nullptr;
 static_assert(offsetof(abi::arm64::android11::LegacyCache,dex_file)==offsetof(abi::arm64::android14::DualCache,dex_file));
 constexpr auto offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);if(!cache||!Extent(cache,&extent)||extent<offset+8)return false;void*file=nullptr;std::memcpy(&file,static_cast<const char*>(cache)+offset,8);
 dex_mapping_abi::Prefix prefix;if(!file||!Extent(file,&extent)||extent<sizeof(prefix))return false;std::memcpy(&prefix,file,sizeof(prefix));size_t header=0,data=0;
 if(!prefix.begin||!prefix.data_begin||!Extent(prefix.begin,&header)||header<112||!Extent(prefix.data_begin,&data)||prefix.data_size>data)return false;
 uint32_t size=U32(uintptr_t(prefix.begin)+32);if(size<112||size>header)return false;evolution::MappedDexMetadata map{prefix.begin,size,prefix.data_begin,prefix.data_size,0};CodeView view;if(!ReadCodeView(layout_,method,layout_.method.size,map,&view)||!Current(const_cast<SourceProducer*>(this),self))return false;*out=view;return true;
}
template<class Base>bool SourceProducer::Walk(void*self,void*caller,uint32_t pc,const void**out)const noexcept{
 struct Visitor final:Base{const SourceProducer&producer;void*caller;uint32_t pc;const void*found=nullptr;unsigned matches=0;Visitor(const SourceProducer&p,void*m,uint32_t at):producer(p),caller(m),pc(at){}bool VisitFrame()override{void*method=nullptr;if(!producer.stack_.Method(this,sizeof(*this),&method))return false;if(method==caller&&this->shadow_frame){CodeView code;if(!producer.Code(this->thread,method,&code))return false;found=this->shadow_frame;++matches;return false;}return true;}}visitor(*this,caller,pc);
 if(!stack_.Construct(&visitor,sizeof(visitor),self,nullptr,stack_visitor_service::WalkKind::kSkipInlined,false)||!stack_.Walk(&visitor,sizeof(visitor),false)||visitor.matches!=1||!Current(const_cast<SourceProducer*>(this),self))return false;*out=visitor.found;return true;
}
bool SourceProducer::Frame(void*self,void*caller,uint32_t pc,const void**out)const noexcept{if(!out||!Current(const_cast<SourceProducer*>(this),self)||!caller)return false;switch(declaration_){case stack_visitor_service::Declaration::kAndroid11:return Walk<abi::arm64::stack::Visitor11>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid12:return Walk<abi::arm64::stack::Visitor12>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid13To16:return Walk<abi::arm64::stack::Visitor13>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid17:return Walk<abi::arm64::stack::Visitor17>(self,caller,pc,out);}return false;}
bool SourceProducer::GetFrame(void*p,void*self,void*caller,uint32_t pc,const void**out)noexcept{return static_cast<SourceProducer*>(p)->Frame(self,caller,pc,out);}
bool SourceProducer::GetCode(void*p,void*self,void*caller,CodeView*out)noexcept{return static_cast<SourceProducer*>(p)->Code(self,caller,out);}
bool SourceProducer::Resolve(void*p,void*self,void*caller,const InvokeInstruction&i,void**target,void**special)noexcept{auto&s=*static_cast<SourceProducer*>(p);if(!target||!special||!Current(p,self)||i.kind>InvokeKind::Interface)return false;constexpr uint32_t invoke_type[]={2,3,1,0,4};auto*resolved=s.resolve_(s.linker_,self,i.index,caller,invoke_type[unsigned(i.kind)]);resolution={&s,self,resolved==nullptr};if(!Current(p,self))return false;*target=resolved;*special=i.kind==InvokeKind::Super?s.metadata_->Reference(caller,0):nullptr;return resolved!=nullptr;}
bool SourceProducer::Pending(void*p,void*self)noexcept{return Current(p,self)&&resolution.owner==p&&resolution.self==self&&resolution.original_null;}
BytecodeAccess SourceProducer::Access()noexcept{return {this,Current,GetFrame,GetCode,Resolve,Pending};}
}

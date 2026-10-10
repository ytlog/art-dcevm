#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_receiver_code_item.h"
#include "platform/common/native_receiver_resolution_source.h"
#include "platform/common/native_receiver_checked_caller.h"
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
bool SourceProducer::Bind(HookEngine&hooks,stack_visitor_service::Declaration declaration,const code_item_metadata::Layout&layout,const modern_field_metadata::FieldMetadata&metadata,void*linker,void*(*current)(),std::string*error,JavaVM*vm){
 if(bound_||!metadata.Calibrated()||!linker||!current){if(error)*error="Receiver producer needs exact calibrated metadata/current Thread/ClassLinker";return false;}
 stack_visitor_service::Service stack;auto finder=[&](const char*n){return hooks.FindSymbol("libart.so",n);};
 // Fixed runtime/class_linker.h: ResolveMode::kCheckICCEAndIAE=1, exact
 // Thread/referrer overload. Its null return has the original Java exception.
 auto resolve=reinterpret_cast<ResolveMethod>(finder(kCheckedResolveMethod));
 auto root_reference=reinterpret_cast<void*(*)(void*)>(finder("artReadBarrierForRootSlow"));
 if(!root_reference){if(error)*error="Receiver source named root-slot barrier unavailable";return false;}
 if(!stack_visitor_service::Service::Bind(declaration,finder,&stack)){if(error)*error="Receiver source calibrated StackVisitor named declaration unavailable";return false;}
 // StackVisitor geometry is shared by multiple releases. Choose the resolver
 // from its own exact declarations, never from the scanner family's label.
 using namespace abi::arm64::android17;
 auto klass=reinterpret_cast<ResolveMethodClass>(finder(kResolveMethodClass));
 auto member=reinterpret_cast<FindResolvedMethod>(finder(kFindResolvedMethod));
 auto checked=reinterpret_cast<CheckedNterpMethod>(finder(kCheckedNterpMethod));
 ResolutionSource source;const char* diagnostic=nullptr;
 if(!SelectResolutionSource(resolve!=nullptr,klass!=nullptr,member!=nullptr,
                            checked!=nullptr,vm!=nullptr,&source,&diagnostic)){
  if(error)*error=diagnostic;return false;
 }
 if(source==ResolutionSource::Nterp){
  if(layout.storage!=code_item_metadata::Storage::StandardPointer||layout.method.size!=sizeof(method_metadata::Method12)||layout.method.dex_method_index!=offsetof(method_metadata::Method12,dex_method_index)||layout.method.data!=offsetof(method_metadata::Method12,data)){if(error)*error="Receiver Nterp source requires its calibrated Standard Method12 declaration";return false;}
  auto clear=reinterpret_cast<ClearCallerAccessChecks>(finder(kClearCallerAccessChecks));
  if(!clear){if(error)*error="Receiver source named Class::ClearSkipAccessChecksFlagOnAllMethods unavailable";return false;}
  clear_caller17_=clear;vm_=vm;resolve_class17_=klass;find_method17_=member;checked_method17_=checked;
 }
 obsolete_cache_=reinterpret_cast<void*(*)(void*)>(finder("_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE0EEENS_6ObjPtrINS_6mirror8DexCacheEEEv"));
 stack_=stack;declaration_=declaration;layout_=layout;metadata_=&metadata;linker_=linker;current_=current;resolve_=resolve;root_reference_=root_reference;bound_=true;return true;
}
bool SourceProducer::Current(void*p,void*self)noexcept{auto&s=*static_cast<SourceProducer*>(p);return s.bound_&&self&&s.current_&&s.current_()==self;}
bool SourceProducer::Code(void*self,void*method,CodeView*out)const noexcept{
 return CodeImpl(self,method,out,nullptr);
}
bool SourceProducer::Code(void*self,void*method,CodeView*out,const modern_field_metadata::ReadContext&context)const noexcept{
 if(!context.owner||!context.extent)return false;return CodeImpl(self,method,out,&context);
}
bool SourceProducer::CodeImpl(void*self,void*method,CodeView*out,const modern_field_metadata::ReadContext*context,evolution::MappedDexMetadata*mapping)const noexcept{
 if(!out||!Current(const_cast<SourceProducer*>(this),self)||!method)return false;size_t extent=0;method_metadata::Identity identity;
 const auto readable=[&](const void*p,size_t*n){return modern_field_metadata::ReadMappingExtent(context,p,n);};
 if(!readable(method,&extent)||!method_metadata::ReadIdentity(layout_.method,method,extent,&identity)||identity.dex_method_index==UINT32_MAX||(identity.access_flags&0x500u))return false;
 // ArtMethod's first field is a GcRoot, not a mirror Object followed by a
 // monitor. Its access_flags word cannot select an object read-barrier state.
 void*klass=root_reference_(method);
 // Fixed ArtMethod::GetDexCache selects the retained obsolete cache instead
 // of a restored declaring Class whose current DEX can already be different.
 void*cache=nullptr;if(identity.access_flags&0x40000u){if(!obsolete_cache_)return false;cache=obsolete_cache_(method);}else {constexpr size_t cache_offset=offsetof(class_metadata::Class11,dex_cache);if(!klass||!readable(klass,&extent)||extent<cache_offset+4)return false;cache=metadata_->Reference(klass,cache_offset);}
 static_assert(offsetof(abi::arm64::android11::LegacyCache,dex_file)==offsetof(abi::arm64::android14::DualCache,dex_file));
 constexpr auto offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);if(!cache||!readable(cache,&extent)||extent<offset+8)return false;void*file=nullptr;std::memcpy(&file,static_cast<const char*>(cache)+offset,8);
 dex_mapping_abi::Prefix prefix;if(!file||!readable(file,&extent)||extent<sizeof(prefix))return false;std::memcpy(&prefix,file,sizeof(prefix));size_t header=0,data=0;
 if(!prefix.begin||!prefix.data_begin||!readable(prefix.begin,&header)||header<112||!readable(prefix.data_begin,&data)||prefix.data_size>data)return false;
 uint32_t size=U32(uintptr_t(prefix.begin)+32);if(size<112||size>header)return false;evolution::MappedDexMetadata map{prefix.begin,size,prefix.data_begin,prefix.data_size,0};CodeView view;if(!ReadCodeView(layout_,method,layout_.method.size,map,&view)||!Current(const_cast<SourceProducer*>(this),self))return false;if(mapping)*mapping=map;*out=view;return true;
}
template<class Base>bool SourceProducer::Walk(void*self,void*caller,uint32_t pc,const void**out)const noexcept{
 struct Visitor final:Base{const SourceProducer&producer;void*caller;uint32_t pc;const void*found=nullptr;unsigned matches=0;Visitor(const SourceProducer&p,void*m,uint32_t at):producer(p),caller(m),pc(at){}bool VisitFrame()override{void*method=nullptr;if(!producer.stack_.Method(this,sizeof(*this),&method))return false;if(method==caller&&this->shadow_frame){CodeView code;if(!producer.Code(this->thread,method,&code))return false;found=this->shadow_frame;++matches;return false;}return true;}}visitor(*this,caller,pc);
 if(!stack_.Construct(&visitor,sizeof(visitor),self,nullptr,stack_visitor_service::WalkKind::kSkipInlined,false)||!stack_.Walk(&visitor,sizeof(visitor),false)||visitor.matches!=1||!Current(const_cast<SourceProducer*>(this),self))return false;*out=visitor.found;return true;
}
bool SourceProducer::Frame(void*self,void*caller,uint32_t pc,const void**out)const noexcept{if(!out||!Current(const_cast<SourceProducer*>(this),self)||!caller)return false;switch(declaration_){case stack_visitor_service::Declaration::kAndroid11:return Walk<abi::arm64::stack::Visitor11>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid12:return Walk<abi::arm64::stack::Visitor12>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid13To16:return Walk<abi::arm64::stack::Visitor13>(self,caller,pc,out);case stack_visitor_service::Declaration::kAndroid17:return Walk<abi::arm64::stack::Visitor17>(self,caller,pc,out);}return false;}
bool SourceProducer::GetFrame(void*p,void*self,void*caller,uint32_t pc,const void**out)noexcept{return static_cast<SourceProducer*>(p)->Frame(self,caller,pc,out);}
bool SourceProducer::GetCode(void*p,void*self,void*caller,CodeView*out)noexcept{return static_cast<SourceProducer*>(p)->Code(self,caller,out);}
bool SourceProducer::Resolve(void*p,void*self,void*caller,uint32_t pc,const InvokeInstruction&i,void**target,void**special)noexcept{auto&s=*static_cast<SourceProducer*>(p);if(!target||!special||!Current(p,self)||i.kind>InvokeKind::Interface)return false;constexpr uint32_t invoke_type[]={2,3,1,0,4};void*resolved=nullptr;if(s.checked_method17_){const bool success=s.Resolve17(self,caller,pc,i,&resolved);bool pending=false;if(!s.Pending17(self,&pending))return false;resolution={&s,self,pending};if(!success&&!pending)return false;}else{resolved=s.resolve_(s.linker_,self,i.index,caller,invoke_type[unsigned(i.kind)]);resolution={&s,self,resolved==nullptr};}if(!Current(p,self))return false;*target=resolved;*special=i.kind==InvokeKind::Super?s.root_reference_(caller):nullptr;return resolved!=nullptr;}
bool SourceProducer::Pending17(void*self,bool*out)const noexcept {
 if(!out||!vm_||!Current(const_cast<SourceProducer*>(this),self))return false;
 JNIEnv*env=nullptr;if(vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)return false;
 const bool pending=env->ExceptionCheck();if(!Current(const_cast<SourceProducer*>(this),self))return false;*out=pending;return true;
}
bool SourceProducer::Resolve17(void*self,void*caller,uint32_t pc,const InvokeInstruction&i,void**out)const noexcept {
 if(!out||!checked_method17_||!resolve_class17_||!find_method17_||i.kind>InvokeKind::Interface)return false;
 bool pending=false;if(!Pending17(self,&pending)||pending)return false;
 const CheckedCallerAccess checks{const_cast<SourceProducer*>(this),Current,FrameExtent,CallerDeclaring,clear_caller17_};
 if(!EnsureCheckedCaller(checks,self,caller))return false;
 CodeView code;evolution::MappedDexMetadata map;InvokeInstruction actual;
 if(!CodeImpl(self,caller,&code,nullptr,&map)||!DecodeInvoke(code,pc,&actual)||actual.kind!=i.kind||actual.index!=i.index||actual.range!=i.range||actual.receiver_register!=i.receiver_register||actual.argument_count!=i.argument_count)return false;
 uint16_t type=0;if(!ReadInvokeClassIndex(map,i.index,&type))return false;
 const auto resolved_class=resolve_class17_(linker_,{type},caller);
 if(!resolved_class.reference||!Current(const_cast<SourceProducer*>(this),self))return false;
 // ResolveType can suspend. Reread all roots used by FindResolvedMethod now,
 // without a JNI transition or a second resolving call between these reads.
 void*owner=root_reference_(caller);size_t extent=0;
 constexpr size_t cache_offset=offsetof(class_metadata::Class11,dex_cache);
 if(!owner||!modern_field_metadata::ReadMappingExtent(nullptr,owner,&extent)||extent<cache_offset+4)return false;
 void*cache=nullptr;method_metadata::Identity caller_identity;
 if(!modern_field_metadata::ReadMappingExtent(nullptr,caller,&extent)||!method_metadata::ReadIdentity(layout_.method,caller,extent,&caller_identity))return false;
 if(caller_identity.access_flags&0x40000u){if(!obsolete_cache_)return false;cache=obsolete_cache_(caller);}else cache=metadata_->Reference(owner,cache_offset);
 constexpr size_t loader_offset=offsetof(class_metadata::Class11,class_loader);
 if(!cache||!modern_field_metadata::ReadMappingExtent(nullptr,owner,&extent)||extent<loader_offset+4)return false;
 void*loader=metadata_->Reference(owner,loader_offset);
 void*resolved=find_method17_(linker_,resolved_class,{uintptr_t(cache)},{uintptr_t(loader)},i.index);
 if(!Pending17(self,&pending)||pending)return false;
 // The original source performs ICCE/IAE checking and super selection. Its
 // registers fast path is optional; nullptr selects its original caller/DEX
 // path. Do not synthesize a managed register array or bypass these checks.
 const uintptr_t carrier=checked_method17_(self,caller,code.instructions+pc,nullptr);
 if(!Pending17(self,&pending)||pending||!resolved)return false;
 method_metadata::Identity identity;if(!modern_field_metadata::ReadMappingExtent(nullptr,resolved,&extent)||!method_metadata::ReadIdentity(layout_.method,resolved,extent,&identity)||identity.dex_method_index==UINT32_MAX||extent<sizeof(method_metadata::Method12))return false;
 uint16_t index=0;std::memcpy(&index,static_cast<const char*>(resolved)+offsetof(method_metadata::Method12,method_index),2);
 void*declaring=root_reference_(resolved);field_copy::CopyClassView klass;
 if(!declaring||!metadata_->ClassIdentity(declaring,&klass,nullptr)||!klass.descriptor)return false;
 abi::arm64::android17::ResolvedDispatch facts{uintptr_t(resolved),index,identity.access_flags,!std::strcmp(klass.descriptor,"Ljava/lang/Object;"),!std::strcmp(klass.descriptor,"Ljava/lang/String;")};uintptr_t selected=0;
 if(!abi::arm64::android17::DecodeCheckedMethod(static_cast<abi::arm64::android17::MethodDispatch>(i.kind),carrier,facts,&selected))return false;
 if(!modern_field_metadata::ReadMappingExtent(nullptr,reinterpret_cast<void*>(selected),&extent)||!method_metadata::ReadIdentity(layout_.method,reinterpret_cast<void*>(selected),extent,&identity)||identity.dex_method_index==UINT32_MAX||!Current(const_cast<SourceProducer*>(this),self))return false;
 *out=reinterpret_cast<void*>(selected);return true;
}
bool SourceProducer::Pending(void*p,void*self)noexcept{return Current(p,self)&&resolution.owner==p&&resolution.self==self&&resolution.original_null;}
BytecodeAccess SourceProducer::Access()noexcept{return {this,Current,GetFrame,GetCode,Resolve,Pending};}
void* SourceProducer::CallerDeclaring(void*p,void*self,void*method)noexcept{auto&s=*static_cast<SourceProducer*>(p);if(!method||!s.root_reference_||!Current(p,self))return nullptr;void*klass=s.root_reference_(method);return Current(p,self)?klass:nullptr;}
bool SourceProducer::FrameExtent(void*p,void*self,const void*address,size_t*out)noexcept{return Current(p,self)&&Extent(address,out)&&Current(p,self);}
bool SourceProducer::FrameReference(void*p,void*self,uint32_t reference,void**out)noexcept{
 auto&s=*static_cast<SourceProducer*>(p);if(!out||!Current(p,self)||!s.root_reference_)return false;
 // Fixed ShadowFrame roots are compressed GcRoot slots, not mirror objects.
 // The root barrier consumes the slot address; no synthetic monitor/header.
 void*object=reference?s.root_reference_(&reference):nullptr;
 if((reference&&!object)||!Current(p,self))return false;*out=object;return true;
}
FrameAccess SourceProducer::Frames()noexcept{return {this,Current,FrameExtent,FrameReference};}
}

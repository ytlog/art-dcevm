#pragma once
#include "platform/common/native_shadow_frame_source.h"
namespace shadow_source_fixture {
// Named-leaf host protocol model; no test-supplied permission callback exists.
struct Shadow {void*method;uint32_t pc;};
struct Visitor final:abi::arm64::stack::Visitor13 {bool VisitFrame()override{return false;}};
inline void*Method(const void*raw){auto*v=static_cast<const Visitor*>(raw);return v->shadow_frame?static_cast<const Shadow*>(v->shadow_frame)->method:nullptr;}
inline uint32_t DexPc(const void*raw,bool abort){assert(!abort);auto*v=static_cast<const Visitor*>(raw);return v->shadow_frame?static_cast<const Shadow*>(v->shadow_frame)->pc:UINT32_MAX;}
inline void Construct(void*raw,void*thread,void*context,stack_visitor_service::WalkKind kind,bool checked){auto*v=static_cast<Visitor*>(raw);v->thread=thread;v->context=context;v->kind=kind;v->check_suspended=checked;v->quick_frame=nullptr;v->shadow_frame=nullptr;}
inline void Walk(void*,bool){}
inline void*Symbol(const char*name){using S=stack_visitor_service::Service;if(!std::strcmp(name,S::MethodSymbol()))return reinterpret_cast<void*>(Method);if(!std::strcmp(name,S::DexPcSymbol()))return reinterpret_cast<void*>(DexPc);if(!std::strcmp(name,S::ConstructSymbol()))return reinterpret_cast<void*>(Construct);if(!std::strcmp(name,S::WalkSymbol(stack_visitor_service::Declaration::kAndroid13To16)))return reinterpret_cast<void*>(Walk);return nullptr;}
struct Context{Shadow shadow{};Visitor visitor;};
inline void Test(Context&context,native_managed_stack_chain::Chain&chain,const source_handle_roots::Service&roots,const native_source_stop::Session&stop,
 const native_cache::Service&threads,modern_field_metadata::ReadContext&read,
 native_boot_aot_unaffected::Source&window,const native_boot_method_catalog::Rooted&catalog,const source_handle_roots::StoppedReader&reader,void*method,size_t card){
 using Fragment=abi::arm64::android16::ManagedStack;auto*head=reinterpret_cast<Fragment*>(foreign_thread.data()+card+offsetof(abi::arm64::android_common::HandleThreadPrefix,managed_stack));const auto saved=*head;
 auto&shadow=context.shadow;auto&visitor=context.visitor;shadow={method,0};
 stack_visitor_service::Service stack;assert(stack_visitor_service::Service::Bind(stack_visitor_service::Declaration::kAndroid13To16,Symbol,&stack)&&stack.BindDexPc(Symbol));assert(stack.Construct(&visitor,sizeof(visitor),foreign_thread.data(),nullptr,stack_visitor_service::WalkKind::kIncludeInlined));visitor.shadow_frame=&shadow;
 *head={};head->top_shadow_frame_=&shadow;assert(chain.Capture(roots,stop,thread.data(),foreign_thread.data(),threads,read).complete);
 native_shadow_frame_source::Source source(stack,chain,window,catalog,reader,method_metadata::kAndroid12Onward);native_shadow_frame_source::Receipt empty,receipt;const char*failure=nullptr;
 assert(!source.Validate(empty,read,&failure));if(!source.Observe(&visitor,sizeof(visitor),read,&receipt,&failure)){std::fprintf(stderr,"Shadow Observe: %s\n",failure?failure:"unknown");std::abort();}assert(receipt.Method()==method&&receipt.DexPc()==0&&receipt.Code().code_item);assert(source.Validate(receipt,read,&failure));
 shadow.pc=1;assert(!source.Validate(receipt,read,&failure));shadow.pc=0;assert(source.Validate(receipt,read,&failure));
 visitor.quick_frame=reinterpret_cast<void**>(&shadow);assert(!source.Validate(receipt,read,&failure));visitor.quick_frame=nullptr;
 visitor.thread=thread.data();assert(!source.Validate(receipt,read,&failure));visitor.thread=foreign_thread.data();
 head->top_shadow_frame_=nullptr;assert(!source.Validate(receipt,read,&failure));head->top_shadow_frame_=&shadow;assert(source.Validate(receipt,read,&failure));
 auto*native=static_cast<abi::arm64::android12::Method12*>(method);const auto index=native->dex_method_index;native->dex_method_index=UINT32_MAX;assert(!source.Validate(receipt,read,&failure));native->dex_method_index=index;assert(source.Validate(receipt,read,&failure));
 native_shadow_frame_source::Source foreign(stack,chain,window,catalog,reader,method_metadata::kAndroid12Onward);assert(!foreign.Validate(receipt,read,&failure));
 assert(!source.Observe(&visitor,sizeof(visitor)-1,read,&receipt,&failure));assert(!receipt.Method()&&!source.Validate(receipt,read,&failure));
 *head=saved;
}
}

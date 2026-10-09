#pragma once
#include "platform/common/native_nterp_frame_reader.h"
#include "platform/common/stack_visitor_service.h"
#include <array>
namespace artdcevm::native_nterp_frame_proof {
using Declaration=stack_visitor_service::Declaration;
using Witness=native_nterp_frame_reader::Witness;
struct Frame {native_nterp_frame_reader::Oracle dex;const Witness* witnesses;size_t count;};
struct Report {bool accepted=false;uint32_t callbacks=0,frames=0,integers=0,references=0,mask=0;const char* failure="not-attempted";};
// Independent three-physical-caller evidence. This does not authorize the
// unrelated whole-process graph or schema continuation/recovery by itself.
class Service {
 public:
 template<class Finder>static bool Bind(Declaration declaration,native_nterp_frame_reader::CanonicalDeclaration canonical,Finder finder,Service*out){
  if(!out||declaration==Declaration::kAndroid11)return false;
  Service s;s.declaration_=declaration;
  if(!stack_visitor_service::Service::Bind(declaration,finder,&s.stack_)||!native_nterp_frame_reader::Service::Bind(canonical,finder,&s.reader_))return false;
  *out=s;return true;
 }
 Report Prove(void* target,void* prepared_context,const std::array<Frame,3>& frames,const modern_field_metadata::ReadContext&context)const noexcept {
  Report r;if(!target||!prepared_context||!context.owner||!context.extent||!Valid(frames)){r.failure="missing-independent-Nterp-inputs";return r;}
  switch(declaration_){
   case Declaration::kAndroid12:Walk<abi::arm64::android12::CodeInfoPair>(target,prepared_context,frames,context,r);break;
   case Declaration::kAndroid13To16:Walk<abi::arm64::android13::CodeInfoPair>(target,prepared_context,frames,context,r);break;
   case Declaration::kAndroid17:Walk<abi::arm64::android17::CodeInfoPair>(target,prepared_context,frames,context,r);break;
   default:r.failure="Nterp-source-declaration-unavailable";return r;
  }
  if(r.mask==7&&r.frames==3&&r.integers==3&&r.references==3&&r.failure==nullptr){r.accepted=true;r.failure="";}
  else if(!r.failure)r.failure="incomplete-Nterp-physical-caller-chain";
  return r;
 }
 private:
 static bool Valid(const std::array<Frame,3>&frames)noexcept {
  for(size_t i=0;i<3;++i){const auto&f=frames[i];if(!f.dex.method||!f.dex.registers||f.dex.registers>16||f.dex.invoke_dex_pc==UINT32_MAX||!f.witnesses||f.count!=2)return false;
   bool integer=false,reference=false;for(size_t n=0;n<2;++n){const auto&w=f.witnesses[n];if(w.vreg>=f.dex.registers)return false;integer|=w.kind==native_nterp_frame_reader::Kind::kInt;reference|=w.kind==native_nterp_frame_reader::Kind::kReference&&w.expected!=0;
    for(size_t prior=0;prior<i;++prior)for(size_t p=0;p<frames[prior].count;++p)if(frames[prior].witnesses[p].kind==w.kind&&frames[prior].witnesses[p].expected==w.expected)return false;
   }
   if(!integer||!reference||f.witnesses[0].vreg==f.witnesses[1].vreg)return false;
   for(size_t prior=0;prior<i;++prior)if(f.dex.method==frames[prior].dex.method)return false;
  }return true;
 }
 template<class Pair>struct Visitor final:abi::arm64::stack::Visitor<Pair> {
  unsigned char tail[sizeof(abi::arm64::stack::Visitor12)-sizeof(abi::arm64::stack::Visitor<Pair>)+8]{};
  const Service& service;void* target;void* prepared;const std::array<Frame,3>&frames;const modern_field_metadata::ReadContext&read_context;Report&report;void** physical[3]{};
  Visitor(const Service&s,void*t,void*p,const std::array<Frame,3>&f,const modern_field_metadata::ReadContext&c,Report&r):service(s),target(t),prepared(p),frames(f),read_context(c),report(r){}
  bool Reject(const char*failure){report.failure=failure;return false;}
  bool VisitFrame()override {
   ++report.callbacks;void*method=nullptr;if(!service.stack_.Method(this,sizeof(*this),&method))return Reject("Nterp-original-GetMethod-refused");
   size_t index=3;for(size_t i=0;i<3;++i)if(method==frames[i].dex.method)index=i;if(index==3)return true;
   const uint32_t bit=1u<<index;
   if(bit!=(report.mask==0?4u:report.mask==4?2u:1u)||(report.mask&bit))return Reject("Nterp-caller-order-or-duplicate");
   if(this->thread!=target||this->context!=prepared||this->shadow_frame||!this->quick_frame||!this->header||this->inline_frames.begin.row!=this->inline_frames.end.row)return Reject("Nterp-native-physical-context-mismatch");
   for(size_t p=0;p<3;++p)if(physical[p]==this->quick_frame)return Reject("Nterp-physical-frame-alias");
   native_nterp_frame_reader::Snapshot snapshot;
   if(!service.reader_.Prove(this,this->quick_frame,this->header,this->pc,frames[index].dex,frames[index].witnesses,frames[index].count,read_context,&snapshot))return Reject("Nterp-native-array-PC-value-proof-refused");
   physical[index]=this->quick_frame;report.mask|=bit;++report.frames;
   for(size_t n=0;n<frames[index].count;++n)if(frames[index].witnesses[n].kind==native_nterp_frame_reader::Kind::kInt)++report.integers;else ++report.references;
   return report.mask!=7;
  }
 };
 template<class Pair>void Walk(void*t,void*p,const std::array<Frame,3>&f,const modern_field_metadata::ReadContext&c,Report&r)const noexcept {
  Visitor<Pair> visitor(*this,t,p,f,c,r);r.failure=nullptr;
  if(!stack_.Construct(&visitor,sizeof(visitor),t,p,stack_visitor_service::WalkKind::kIncludeInlined,true)||visitor.thread!=t||visitor.context!=p||visitor.kind!=stack_visitor_service::WalkKind::kIncludeInlined){r.failure="Nterp-constructor-context-geometry-mismatch";return;}
  if(!stack_.Walk(&visitor,sizeof(visitor),false))r.failure="Nterp-original-walk-refused";
 }
 Declaration declaration_=Declaration::kAndroid12;stack_visitor_service::Service stack_;native_nterp_frame_reader::Service reader_;
};
}

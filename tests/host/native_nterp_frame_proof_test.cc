#include "platform/common/native_nterp_frame_proof.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
namespace p=native_nterp_frame_proof;namespace n=native_nterp_frame_reader;namespace a=abi::arm64;
alignas(16)unsigned char storage[3][128]{};int methods[3],target,context,header;
stack_visitor_service::Declaration declaration;void* current_method;bool duplicate=false,inline_frame=false,wrong_context=false;unsigned selected=0;
void** Frame(unsigned i){return reinterpret_cast<void**>(storage[i]);}
uintptr_t Refs(void**f){return reinterpret_cast<uintptr_t>(f)+40;}
uintptr_t Regs(void**f){return Refs(f)+12;}
n::FrameInfo Info(void**){return {96,15,0};}
uint32_t DexPc(void**f){for(unsigned i=0;i<3;++i)if(f==Frame(i))return 19+i;std::abort();}
uint32_t Value(void**f,uint16_t reg){uint32_t v;std::memcpy(&v,reinterpret_cast<void*>(Regs(f)+reg*4),4);return v;}
uint32_t Reference(void**f,uint16_t reg){uint32_t v;std::memcpy(&v,reinterpret_cast<void*>(Refs(f)+reg*4),4);return v;}
const void* MethodHeader(void*m,uintptr_t pc){for(unsigned i=0;i<3;++i)if(m==&methods[i]&&pc==101+i)return &header;return nullptr;}
bool Contains(const void*h,uintptr_t pc){return h==&header&&pc>=101&&pc<=103;}
bool Nterp(const void*h){return h==&header;}
template<class Pair>auto* Prefix(void*v){return static_cast<a::stack::Visitor<Pair>*>(v);}
template<class Pair>void ConstructImpl(void*v,void*t,void*c,a::stack::WalkKind k,bool check){auto*f=Prefix<Pair>(v);assert(check);f->thread=t;f->context=c;f->kind=k;}
void Construct(void*v,void*t,void*c,a::stack::WalkKind k,bool check){if(declaration==p::Declaration::kAndroid12)ConstructImpl<a::android12::CodeInfoPair>(v,t,c,k,check);else if(declaration==p::Declaration::kAndroid17)ConstructImpl<a::android17::CodeInfoPair>(v,t,c,k,check);else ConstructImpl<a::android13::CodeInfoPair>(v,t,c,k,check);}
template<class Pair>void WalkImpl(void*v){auto*f=Prefix<Pair>(v);for(int i=2;i>=0;--i){selected=duplicate?2:i;current_method=&methods[selected];f->quick_frame=Frame(selected);f->pc=101+selected;f->header=&header;f->shadow_frame=nullptr;f->inline_frames.begin.row=0;f->inline_frames.end.row=inline_frame?1:0;if(wrong_context)f->context=&header;if(!f->VisitFrame())return;}}
void Walk(void*v,bool transitions){assert(!transitions);if(declaration==p::Declaration::kAndroid12)WalkImpl<a::android12::CodeInfoPair>(v);else if(declaration==p::Declaration::kAndroid17)WalkImpl<a::android17::CodeInfoPair>(v);else WalkImpl<a::android13::CodeInfoPair>(v);}
void* GetMethod(const void*){return current_method;}
uint32_t VisitorPc(const void*,bool approximate){assert(!approximate);return 19+selected;}
bool Debugger(const void*,uint16_t,n::Kind,uint32_t*){return false;}
bool Canonical(const void*,void*m,uint16_t reg,n::Kind kind,uint32_t*out,a::android11::frame_registers::OptionalLocation loc){assert(m==current_method&&!loc);*out=kind==n::Kind::kInt?Value(Frame(selected),reg):Reference(Frame(selected),reg);return true;}
bool CanonicalFull(const void*v,void*m,uint16_t r,n::Kind k,uint32_t*out,a::android14::frame_registers::OptionalLocation loc,bool full){assert(!full);return Canonical(v,m,r,k,out,loc);}
bool Range(void*owner,const void*value,size_t*out)noexcept {assert(owner==&target);auto at=reinterpret_cast<uintptr_t>(value);for(auto&f:storage){auto base=reinterpret_cast<uintptr_t>(f);if(at>=base&&at<base+sizeof(f)){*out=base+sizeof(f)-at;return true;}}return false;}
struct Finder{template<class F>static void*Fn(F f){return reinterpret_cast<void*>(f);}void*operator()(const char*s)const {
#define LEAF(name,f) if(!std::strcmp(s,name))return Fn(f)
 LEAF(a::stack::kConstruct,Construct);LEAF(a::stack::kWalk,Walk);LEAF(a::stack::kWalk17,Walk);LEAF(a::stack::kMethod,GetMethod);
 LEAF(n::Service::kReferences,Refs);LEAF(n::Service::kRegisters,Regs);LEAF(n::Service::kFrameInfo,Info);LEAF(n::Service::kDexPc,DexPc);LEAF(n::Service::kValue,Value);LEAF(n::Service::kReference,Reference);
 LEAF(native_frame_code_owner::Service::kMethodHeader,MethodHeader);LEAF(native_frame_code_owner::Service::kContains,Contains);LEAF(native_frame_code_owner::Service::kNterp,Nterp);
 LEAF("_ZNK3art12StackVisitor8GetDexPcEb",VisitorPc);LEAF(a::android14::frame_registers::kDebuggerValue,Debugger);LEAF(a::android12::frame_registers::kCanonical,Canonical);LEAF(a::android14::frame_registers::kCanonical,CanonicalFull);
#undef LEAF
 return nullptr;}};
}
int main(){
 std::array<p::Frame,3> frames;n::Witness witnesses[3][2];
 for(unsigned i=0;i<3;++i){*Frame(i)=&methods[i];uint32_t integer=73+i,reference=0x11000+i*0x1000;std::memcpy(reinterpret_cast<void*>(Regs(Frame(i))),&integer,4);std::memcpy(reinterpret_cast<void*>(Refs(Frame(i))+4),&reference,4);std::memcpy(reinterpret_cast<void*>(Regs(Frame(i))+4),&reference,4);witnesses[i][0]={0,n::Kind::kInt,integer};witnesses[i][1]={1,n::Kind::kReference,reference};frames[i]={{&methods[i],3,3,19+i},witnesses[i],2};}
 for(auto d:{p::Declaration::kAndroid12,p::Declaration::kAndroid13To16,p::Declaration::kAndroid17})for(auto c:{n::CanonicalDeclaration::WithoutFullRegisterList,n::CanonicalDeclaration::WithFullRegisterList}){declaration=d;p::Service reader;assert(p::Service::Bind(d,c,Finder{},&reader));auto run=[&]{return reader.Prove(&target,&context,frames,{&target,Range});};
  auto report=run();assert(report.accepted&&report.frames==3&&report.mask==7&&report.references==3&&report.integers==3);
  duplicate=true;assert(!run().accepted);duplicate=false;
  inline_frame=true;assert(!run().accepted);inline_frame=false;
  wrong_context=true;assert(!run().accepted);wrong_context=false;
  ++witnesses[1][1].expected;assert(!run().accepted);--witnesses[1][1].expected;
  uint32_t fresh=witnesses[1][1].expected+0x10000;std::memcpy(reinterpret_cast<void*>(Refs(Frame(1))+4),&fresh,4);std::memcpy(reinterpret_cast<void*>(Regs(Frame(1))+4),&fresh,4);assert(!run().accepted);witnesses[1][1].expected=fresh;assert(run().accepted);
 }
 std::cout<<"Actual original StackVisitor constructor/walk + three distinct Nterp physical frames/CodeItems/canonical arrays + duplicate/inline/context/stale-root refusals PASS\n";
 return 0;
}

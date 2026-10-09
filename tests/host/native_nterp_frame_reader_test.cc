#include "platform/common/native_nterp_frame_reader.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace artdcevm;
namespace {
namespace n=native_nterp_frame_reader;
alignas(16)std::array<unsigned char,128> bytes{};int method,header,visitor,wrong_method;
bool stopped=true,debug_override=false,bad_array=false,bad_frame=false,drift=false,bad_header=false;
unsigned arrays_read=0;constexpr uint16_t native_registers=3,native_outs=3;
uintptr_t Base(){return reinterpret_cast<uintptr_t>(bytes.data());}
void** Frame(){return reinterpret_cast<void**>(bytes.data());}
uintptr_t References(void** f){assert(f==Frame());++arrays_read;return Base()+24+((native_outs*4+7)&~size_t(7))+(bad_array||(drift&&arrays_read>2)?4:0);}
uintptr_t Registers(void** f){return References(f)+native_registers*4;}
n::FrameInfo Info(void** f){assert(f==Frame());return {bad_frame?80u:96u,15,0};}
uint32_t Pc(void** f){assert(f==Frame());return 19;}
uint32_t VisitorPc(const void*v,bool approximate){assert(v==&visitor&&!approximate);return 19;}
uint32_t Value(void** f,uint16_t reg){uint32_t value;std::memcpy(&value,reinterpret_cast<void*>(Registers(f)+reg*4),4);return value;}
uint32_t Reference(void** f,uint16_t reg){uint32_t value;std::memcpy(&value,reinterpret_cast<void*>(References(f)+reg*4),4);return value;}
const void* MethodHeader(void*m,uintptr_t pc){return !bad_header&&m==&method&&pc==101?&header:nullptr;}
bool Contains(const void*h,uintptr_t pc){return h==&header&&pc==101;}
bool Nterp(const void*h){assert(h==&header);return true;}
bool Debugger(const void*v,uint16_t,n::Kind,uint32_t*){assert(v==&visitor);return debug_override;}
bool Canonical12(const void*v,void*m,uint16_t reg,n::Kind kind,uint32_t*out,abi::arm64::android11::frame_registers::OptionalLocation location){
 assert(v==&visitor&&m==&method&&!location.has_value());*out=kind==n::Kind::kReference?Reference(Frame(),reg):Value(Frame(),reg);return true;
}
bool Canonical14(const void*v,void*m,uint16_t reg,n::Kind kind,uint32_t*out,abi::arm64::android14::frame_registers::OptionalLocation location,bool full){assert(!full);return Canonical12(v,m,reg,kind,out,location);}
bool Extent(void*owner,const void*p,size_t*out)noexcept{
 assert(owner==&stopped);if(!stopped)return false;const auto at=reinterpret_cast<uintptr_t>(p);
 if(at<Base()||at>=Base()+bytes.size())return false;*out=Base()+bytes.size()-at;return true;
}
struct Finder {
 const char*missing=nullptr;
 template<class F>static void* Fn(F f){return reinterpret_cast<void*>(f);}
 void* operator()(const char*s)const{
  if(missing&&!std::strcmp(s,missing))return nullptr;
#define LEAF(name,fn) if(!std::strcmp(s,name))return Fn(fn)
  LEAF(n::Service::kReferences,References);LEAF(n::Service::kRegisters,Registers);LEAF(n::Service::kFrameInfo,Info);LEAF(n::Service::kDexPc,Pc);LEAF(n::Service::kValue,Value);LEAF(n::Service::kReference,Reference);
  LEAF(native_frame_code_owner::Service::kMethodHeader,MethodHeader);LEAF(native_frame_code_owner::Service::kContains,Contains);LEAF(native_frame_code_owner::Service::kNterp,Nterp);
  LEAF("_ZNK3art12StackVisitor8GetDexPcEb",VisitorPc);LEAF(abi::arm64::android14::frame_registers::kDebuggerValue,Debugger);
  LEAF(abi::arm64::android12::frame_registers::kCanonical,Canonical12);LEAF(abi::arm64::android14::frame_registers::kCanonical,Canonical14);
#undef LEAF
  return nullptr;
 }
};
}
int main(){
 *Frame()=&method;uint32_t integer=73,root=0x11000;
 std::memcpy(reinterpret_cast<void*>(Base()+52),&integer,4);std::memcpy(reinterpret_cast<void*>(Base()+44),&root,4);
 modern_field_metadata::ReadContext context{&stopped,Extent};
 for(auto declaration:{n::CanonicalDeclaration::WithoutFullRegisterList,n::CanonicalDeclaration::WithFullRegisterList}){
  n::Service service;assert(n::Service::Bind(declaration,Finder{},&service));
  n::Oracle oracle{&method,3,3,19};n::Witness witness[]{{0,n::Kind::kInt,integer},{1,n::Kind::kReference,root}};n::Snapshot output;
  auto run=[&]{arrays_read=0;return service.Prove(&visitor,Frame(),&header,101,oracle,witness,2,context,&output);};
  assert(run()&&output.references==Base()+40&&output.registers==Base()+52&&output.frame_size==96);
  stopped=false;assert(!run());stopped=true;
  debug_override=true;assert(!run());debug_override=false;
  bad_array=true;assert(!run());bad_array=false;
  bad_frame=true;assert(!run());bad_frame=false;
  bad_header=true;assert(!run());bad_header=false;
  drift=true;assert(!run());drift=false;
  oracle.registers=2;assert(!run());oracle.registers=3;
  oracle.outs=2;assert(!run());oracle.outs=3;
  ++oracle.invoke_dex_pc;assert(!run());--oracle.invoke_dex_pc;
  *Frame()=&wrong_method;assert(!run());*Frame()=&method;
  ++witness[1].expected;assert(!run());--witness[1].expected;
  root+=0x1000;std::memcpy(reinterpret_cast<void*>(Base()+44),&root,4);assert(!run());witness[1].expected=root;assert(run());
  witness[1].vreg=3;assert(!run());witness[1].vreg=1;
  n::Snapshot prior=output;assert(!service.Prove(&visitor,Frame(),&header,101,oracle,witness,2,{nullptr,nullptr},&output));assert(output.references==prior.references);
  for(auto missing:{n::Service::kFrameInfo,n::Service::kReferences,n::Service::kRegisters,n::Service::kReference,n::Service::kValue,native_frame_code_owner::Service::kMethodHeader}){n::Service absent;assert(!n::Service::Bind(declaration,Finder{missing},&absent));}
 }
 std::cout<<"Source-declared ARM64 Nterp arrays/CodeItem counts/saved invoke PC, original canonical+helper+physical values and GC-refreshed witnesses PASS\n";
 return 0;
}

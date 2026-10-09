#pragma once
#include "platform/common/native_frame_code_owner.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/abi/arm64/android12/frame_registers.h"
#include "platform/abi/arm64/android14/frame_registers.h"
#include <cstring>
namespace artdcevm::native_nterp_frame_reader {
using Kind=abi::arm64::android11::frame_registers::Kind;
// Fixed 12/14/16 and17 b753cf9 nterp_helpers.cc, QuickMethodFrameInfo.
// ARM64 only: pointers are 8 bytes, vregs and compressed GC roots are 4 bytes.
struct FrameInfo {uint32_t size,core_spills,fp_spills;};
static_assert(sizeof(FrameInfo)==12);
enum class CanonicalDeclaration {WithoutFullRegisterList,WithFullRegisterList};
struct Oracle {void* method;uint16_t registers,outs;uint32_t invoke_dex_pc;};
struct Witness {uint16_t vreg;Kind kind;uint32_t expected;};
struct Snapshot {uintptr_t references=0,registers=0;uint32_t frame_size=0,dex_pc=0;};
class Service {
 public:
 template<class Finder>static bool Bind(CanonicalDeclaration d,Finder finder,Service*out){
  if(!out)return false;Service s;s.owner_=native_frame_code_owner::Service::Bind(finder);
  s.refs_=reinterpret_cast<Array>(finder(kReferences));s.regs_=reinterpret_cast<Array>(finder(kRegisters));
  s.info_=reinterpret_cast<Info>(finder(kFrameInfo));s.dex_pc_=reinterpret_cast<DexPc>(finder(kDexPc));
  s.value_=reinterpret_cast<Value>(finder(kValue));s.reference_=reinterpret_cast<Value>(finder(kReference));
  s.visitor_pc_=reinterpret_cast<VisitorPc>(finder("_ZNK3art12StackVisitor8GetDexPcEb"));
  s.debugger_=reinterpret_cast<Debugger>(finder(abi::arm64::android14::frame_registers::kDebuggerValue));
  if(d==CanonicalDeclaration::WithFullRegisterList)s.canonical_full_=reinterpret_cast<CanonicalFull>(finder(abi::arm64::android14::frame_registers::kCanonical));
  else s.canonical_=reinterpret_cast<Canonical>(finder(abi::arm64::android12::frame_registers::kCanonical));
  if(!s.owner_.ExactHeaderAvailable()||!s.refs_||!s.regs_||!s.info_||!s.dex_pc_||!s.value_||!s.reference_||!s.visitor_pc_||!s.debugger_||(!s.canonical_&&!s.canonical_full_))return false;
  *out=s;return true;
 }
 bool Prove(void* visitor,void** frame,const void* header,uintptr_t pc,const Oracle&o,
     const Witness*w,size_t count,const modern_field_metadata::ReadContext&context,Snapshot*out)const noexcept {
  if(!visitor||!frame||!header||!o.method||!o.registers||!w||!count||!out||!context.owner||!context.extent||
     !Range(context,frame,8)||*frame!=o.method||owner_.Observe(o.method,pc,header,nullptr)!=native_frame_code_owner::Kind::Nterp)return false;
  const auto info=info_(frame);const size_t spills=__builtin_popcount(info.core_spills)+__builtin_popcount(info.fp_spills);
  // Same fixed ARM64 source computation; no object field/assembly inference.
  const size_t source_size=(spills*8+size_t(o.registers)*8+24+size_t(o.outs)*4+15)&~size_t(15);
  if(!info.size||info.size!=source_size||!Range(context,frame,info.size))return false;
  const uintptr_t base=reinterpret_cast<uintptr_t>(frame);
  if(base>UINTPTR_MAX-info.size)return false;
  const uintptr_t expected_refs=base+24+((size_t(o.outs)*4+7)&~size_t(7));
  const uintptr_t refs=refs_(frame),regs=regs_(frame);
  const size_t bytes=size_t(o.registers)*4;
  if(refs!=expected_refs||regs!=refs+bytes||regs+bytes<regs||regs+bytes>base+info.size||
     !Range(context,reinterpret_cast<void*>(refs),bytes)||!Range(context,reinterpret_cast<void*>(regs),bytes)||
     dex_pc_(frame)!=o.invoke_dex_pc||visitor_pc_(visitor,false)!=o.invoke_dex_pc)return false;
  for(size_t n=0;n<count;++n){
   const auto& item=w[n];if(item.vreg>=o.registers||(item.kind!=Kind::kInt&&item.kind!=Kind::kReference))return false;
   for(size_t prior=0;prior<n;++prior)if(w[prior].vreg==item.vreg&&w[prior].kind==item.kind)return false;
   uint32_t shadow=0;if(debugger_(visitor,item.vreg,item.kind,&shadow))return false;
   uint32_t original=0;
   const bool accepted=canonical_full_?canonical_full_(visitor,o.method,item.vreg,item.kind,&original,std::nullopt,false):canonical_(visitor,o.method,item.vreg,item.kind,&original,std::nullopt);
   if(!accepted)return false;
   const auto array=item.kind==Kind::kReference?refs:regs;
   uint32_t physical=0;std::memcpy(&physical,reinterpret_cast<void*>(array+size_t(item.vreg)*4),4);
   const uint32_t helper=item.kind==Kind::kReference?reference_(frame,item.vreg):value_(frame,item.vreg);
   if(original!=item.expected||physical!=original||helper!=original)return false;
  }
  // No JNI, allocation, mutation or cookie synthesis. Revoke on cursor, owner,
  // saved DexPc, native array or current stopped-context drift after readers.
  if(!Range(context,frame,info.size)||*frame!=o.method||owner_.Observe(o.method,pc,header,nullptr)!=native_frame_code_owner::Kind::Nterp||
     refs_(frame)!=refs||regs_(frame)!=regs||dex_pc_(frame)!=o.invoke_dex_pc||visitor_pc_(visitor,false)!=o.invoke_dex_pc)return false;
  const auto refreshed=info_(frame);
  if(refreshed.size!=info.size||refreshed.core_spills!=info.core_spills||refreshed.fp_spills!=info.fp_spills)return false;
  *out={refs,regs,info.size,o.invoke_dex_pc};return true;
 }
 inline static constexpr char kReferences[]="_ZN3art22NterpGetReferenceArrayEPPNS_9ArtMethodE";
 inline static constexpr char kRegisters[]="_ZN3art22NterpGetRegistersArrayEPPNS_9ArtMethodE";
 inline static constexpr char kFrameInfo[]="_ZN3art14NterpFrameInfoEPPNS_9ArtMethodE";
 inline static constexpr char kDexPc[]="_ZN3art13NterpGetDexPCEPPNS_9ArtMethodE";
 inline static constexpr char kValue[]="_ZN3art12NterpGetVRegEPPNS_9ArtMethodEt";
 inline static constexpr char kReference[]="_ZN3art21NterpGetVRegReferenceEPPNS_9ArtMethodEt";
 private:
 static bool Range(const modern_field_metadata::ReadContext&c,const void*p,size_t n)noexcept {size_t extent=0;return p&&c.extent(c.owner,p,&extent)&&extent>=n;}
 using Array=uintptr_t(*)(void**);using Info=FrameInfo(*)(void**);using DexPc=uint32_t(*)(void**);
 using Value=uint32_t(*)(void**,uint16_t);using VisitorPc=uint32_t(*)(const void*,bool);
 using Debugger=abi::arm64::android14::frame_registers::DebuggerValue;
 using Canonical=abi::arm64::android12::frame_registers::Canonical;using CanonicalFull=abi::arm64::android14::frame_registers::Canonical;
 native_frame_code_owner::Service owner_;
 Array refs_=nullptr,regs_=nullptr;Info info_=nullptr;DexPc dex_pc_=nullptr;Value value_=nullptr,reference_=nullptr;
 VisitorPc visitor_pc_=nullptr;Debugger debugger_=nullptr;Canonical canonical_=nullptr;CanonicalFull canonical_full_=nullptr;
};
}

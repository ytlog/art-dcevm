#pragma once
#include "platform/common/art_method_metadata.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
namespace artdcevm::native_runtime_frame {
// Fixed ARM64 QuickMethodFrameInfo and callee_save_frame_arm64.h. Clinit and
// suspend-check frames canonicalize to the SaveEverything geometry.
struct FrameInfo {uint32_t size,core_spills,fp_spills;};
static_assert(sizeof(FrameInfo)==12);
inline bool Extent(const modern_field_metadata::ReadContext* context,const void* value,size_t* extent)noexcept{
 return context?context->owner&&context->extent&&context->extent(context->owner,value,extent):method_identity::ReadableMappingExtent(value,extent);
}
constexpr uint32_t Bits(uint32_t n){uint32_t count=0;while(n){count+=n&1u;n>>=1;}return count;}
constexpr uint32_t Size(uint32_t core,uint32_t fp){return ((Bits(core)+Bits(fp)+1u)*8u+15u)&~15u;}
inline bool CalleeGeometry(FrameInfo f)noexcept{
 constexpr uint32_t refs=0x7ff00000u; // x20--x30, including LR.
 constexpr FrameInfo known[]={{Size(refs,0),refs,0},
  {Size(refs|0xfeu,0xffu),refs|0xfeu,0xffu},
  {Size(refs|0x80000u,0xff00u),refs|0x80000u,0xff00u},
  {Size(refs|0xbffffu,0xffffffffu),refs|0xbffffu,0xffffffffu}};
 for(auto k:known)if(k.size==f.size&&k.core_spills==f.core_spills&&k.fp_spills==f.fp_spills)return true;
 return false;
}
class Service {
 public:
 template<class Finder>static Service Bind(Finder finder){Service s;s.info_=reinterpret_cast<FrameInfo(*)(const void*)>(finder(kFrameInfo));return s;}
 bool Bound()const noexcept{return info_!=nullptr;}
 bool Observe(const void* visitor,void* method,void** quick,bool legacy,
              const modern_field_metadata::ReadContext* context,FrameInfo* out)const noexcept{
  if(!info_||!visitor||!method||!quick||!out||(uintptr_t(quick)&15u))return false;
  const auto layout=legacy?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  size_t extent=0;method_metadata::Identity identity;
  if(!Extent(context,method,&extent)||
     !method_metadata::ReadIdentity(layout,method,extent,&identity)||identity.dex_method_index!=UINT32_MAX)return false;
  uint32_t declaring=0;uintptr_t entry=0;std::memcpy(&declaring,method,4);
  entry=__atomic_load_n(reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(method)+layout.entry_point),__ATOMIC_RELAXED);
  // Callee-save placeholders have no Java owner/body/entry. Resolution and IMT
  // runtime methods use stub entries; the sentinel alone never grants this path.
  if(declaring||identity.access_flags||entry)return false;
  if(!Extent(context,quick,&extent)||extent<sizeof(void*)||*quick!=method)return false;
  const auto info=info_(visitor);
  if(!CalleeGeometry(info)||extent<info.size||info.size>UINTPTR_MAX-uintptr_t(quick))return false;
  uintptr_t return_pc=0;std::memcpy(&return_pc,reinterpret_cast<unsigned char*>(quick)+info.size-8,8);
  if(!return_pc||*quick!=method)return false;
  *out=info;return true;
 }
 inline static constexpr char kFrameInfo[]="_ZNK3art12StackVisitor24GetCurrentQuickFrameInfoEv";
 private:FrameInfo(*info_)(const void*)=nullptr;
};
}

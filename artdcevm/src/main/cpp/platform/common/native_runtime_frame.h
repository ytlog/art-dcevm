#pragma once
#include "platform/common/modern_field_metadata.h"
#include <cstring>
namespace artdcevm::native_runtime_frame {
// Fixed ARM64 QuickMethodFrameInfo returned by the original StackVisitor.
struct FrameInfo {uint32_t size,core_spills,fp_spills;};
static_assert(sizeof(FrameInfo)==12);
enum class NameStatus:uint32_t {NotObserved,MissingReader,NullResult,UnmappedResult,Truncated,Complete,CursorChanged};
// Diagnostic only. The named source returns process-lifetime literals, never
// Java strings or a receiver/class ownership capability.
class Service {
 public:
 template<class Finder>static Service Bind(Finder finder){Service s;s.info_=reinterpret_cast<FrameInfo(*)(const void*)>(finder(kFrameInfo));s.name_=reinterpret_cast<const char*(*)(void*)>(finder(kRuntimeName));s.return_address_=reinterpret_cast<uintptr_t(*)(const void*)>(finder(kReturnAddress));return s;}
 // Original cursor geometry only. The caller joins this with the actual
 // walker method/kind; it grants no JNI continuation or root permission.
 bool Physical(const void*visitor,void*method,void**quick,
               const modern_field_metadata::ReadContext&read,FrameInfo*out,uintptr_t**return_slot,uintptr_t*return_pc)const noexcept{
  if(!info_||!return_address_||!visitor||!method||!quick||!out||!return_slot||!return_pc||(uintptr_t(quick)&15u)||!read.owner||!read.extent)return false;
  size_t extent=0;if(!read.extent(read.owner,quick,&extent)||extent<sizeof(void*)||*quick!=method)return false;
  const auto info=info_(visitor);
  if(info.size<16||info.size%16||info.size>UINTPTR_MAX-uintptr_t(quick)||extent<info.size)return false;
  // Ask the same original cursor for its continuation slot. Geometry bounds
  // constrain the returned address before any load; no inferred slot offset
  // or alternate reader grants a physical boundary when this leaf is absent.
  const uintptr_t address=return_address_(visitor),base=uintptr_t(quick);
  if(address%alignof(uintptr_t)||address<base+sizeof(void*)||address>base+info.size-sizeof(uintptr_t))return false;
  uintptr_t pc=0;std::memcpy(&pc,reinterpret_cast<const void*>(address),sizeof(pc));
  if(!pc||*quick!=method)return false;*out=info;*return_slot=reinterpret_cast<uintptr_t*>(address);*return_pc=pc;return true;
 }
 NameStatus CopyObservedName(void*method,const modern_field_metadata::ReadContext*context,
                             char*out,size_t capacity)const noexcept{
  if(!out||!capacity)return NameStatus::Truncated;
  out[0]=0;
  if(!name_)return NameStatus::MissingReader;
  const char*name=name_(method);
  if(!name)return NameStatus::NullResult;
  size_t extent=0;
  // Never query /proc in the stopped diagnostic path. Missing prepared bounds
  // leaves the name unavailable, without changing frame acceptance.
  if(!context||!context->owner||!context->extent||!context->extent(context->owner,name,&extent)||!extent)return NameStatus::UnmappedResult;
  const size_t limit=extent<capacity?extent:capacity;
  for(size_t i=0;i<limit;++i){const char c=name[i];if(!c){out[i]=0;return NameStatus::Complete;}if(i+1<capacity)out[i]=c;}
  out[0]=0;return NameStatus::Truncated;
 }
 inline static constexpr char kRuntimeName[]="_ZN3art9ArtMethod20GetRuntimeMethodNameEv";
 inline static constexpr char kFrameInfo[]="_ZNK3art12StackVisitor24GetCurrentQuickFrameInfoEv";
 inline static constexpr char kReturnAddress[]="_ZNK3art12StackVisitor15GetReturnPcAddrEv";
 private:uintptr_t(*return_address_)(const void*)=nullptr;FrameInfo(*info_)(const void*)=nullptr;const char*(*name_)(void*)=nullptr;
};
}

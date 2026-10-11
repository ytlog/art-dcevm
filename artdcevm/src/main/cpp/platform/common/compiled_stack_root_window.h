#pragma once
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/modern_field_metadata.h"
#include <cstring>
#include <limits>
namespace artdcevm::compiled_stack_root_window {
// Root storage geometry only, not Method/CodeItem authority. The closed source
// owner must obtain `code` for this exact physical Method in the same source
// stop before calling BindSameMethodCode. No managed pointer crosses a safepoint.
// ARM64 GetStackSlotOfParameter = frame_bytes + ArtMethod*(8) + index*4.
// ins_size includes `this` and wide words; register assignment does not remove
// the incoming reserved slots. LR remains at physical frame_bytes - 8.
class Window final {
 public:
 static bool BindSameMethodCode(void* method,void** quick,uint32_t frame_bytes,
  const receiver_guard::CodeView& code,const modern_field_metadata::ReadContext& read,Window*out)noexcept{
  if(!out)return false;*out={};
  if(!method||!quick||frame_bytes<sizeof(uintptr_t)||frame_bytes%4||
     !read.owner||!read.extent||!code.code_item||!code.instructions||!code.units||
     code.registers>UINT16_MAX||code.ins_size>code.registers)return false;
  const size_t header=code.compact?4:16;
  const auto item=uintptr_t(code.code_item),instructions=uintptr_t(code.instructions),base=uintptr_t(quick);
  if(item>UINTPTR_MAX-header||instructions!=item+header||code.units>(SIZE_MAX-header)/2||
     code.units>(UINTPTR_MAX-instructions)/2||base>UINTPTR_MAX-frame_bytes||
     base+frame_bytes>UINTPTR_MAX-sizeof(uintptr_t))return false;
  const size_t bytes=header+code.units*2;size_t extent=0;
  if(!read.extent(read.owner,code.code_item,&extent)||extent<bytes)return false;
  const auto&dex=code.source_dex;
  const auto contains=[&](const unsigned char*p,size_t n){const auto start=uintptr_t(p);return p&&n<=UINTPTR_MAX-start&&item>=start&&item-start<=n&&bytes<=n-(item-start);};
  const unsigned char*data=dex.data_begin;size_t data_size=dex.data_size;
  if(!contains(data,data_size)){data=dex.begin;data_size=dex.size;}
  if(!contains(data,data_size)||!read.extent(read.owner,data,&extent)||extent<data_size||
     !dex.begin||dex.size<112||!read.extent(read.owner,dex.begin,&extent)||extent<dex.size)return false;
  if(code.compact?std::memcmp(dex.begin,"cdex",4)!=0:std::memcmp(dex.begin,"dex\n",4)!=0)return false;
  const uintptr_t incoming=base+frame_bytes+sizeof(uintptr_t);
  if(code.ins_size>(UINTPTR_MAX-incoming)/4)return false;
  out->method_=method;out->quick_=quick;out->frame_bytes_=frame_bytes;
  out->code_item_=code.code_item;out->instructions_=code.instructions;out->units_=code.units;
  out->registers_=code.registers;out->ins_size_=code.ins_size;out->compact_=code.compact;
  out->dex_=dex.begin;out->data_=data;out->data_size_=data_size;
  return true;
 }
 bool Slot(void*method,void**quick,uint32_t frame_bytes,uint32_t bit,const void**out)const noexcept{
  if(!out)return false;*out=nullptr;const uintptr_t base=uintptr_t(quick);
  if(!method||!quick||frame_bytes<sizeof(uintptr_t)||frame_bytes%4||
     base>UINTPTR_MAX-frame_bytes||uint64_t(bit)*4>UINTPTR_MAX-base)return false;
  const uintptr_t address=base+uintptr_t(bit)*4;
  if(address<base+frame_bytes){*out=reinterpret_cast<const void*>(address);return true;}
  if(method_!=method||quick_!=quick||frame_bytes_!=frame_bytes||!code_item_||
     base+frame_bytes>UINTPTR_MAX-sizeof(uintptr_t))return false;
  const uintptr_t begin=base+frame_bytes+sizeof(uintptr_t);
  if(ins_size_>(UINTPTR_MAX-begin)/4)return false;
  if(address<begin||address>=begin+uintptr_t(ins_size_)*4)return false;
  *out=reinterpret_cast<const void*>(address);return true;
 }
 bool Same(const Window&o)const noexcept{return method_==o.method_&&quick_==o.quick_&&frame_bytes_==o.frame_bytes_&&code_item_==o.code_item_&&instructions_==o.instructions_&&units_==o.units_&&registers_==o.registers_&&ins_size_==o.ins_size_&&compact_==o.compact_&&dex_==o.dex_&&data_==o.data_&&data_size_==o.data_size_;}
 private:
 void*method_=nullptr;void**quick_=nullptr;uint32_t frame_bytes_=0;
 const void*code_item_=nullptr;const uint16_t*instructions_=nullptr;size_t units_=0;
 uint32_t registers_=0,ins_size_=0;bool compact_=false;
 const unsigned char*dex_=nullptr,*data_=nullptr;size_t data_size_=0;
};
}

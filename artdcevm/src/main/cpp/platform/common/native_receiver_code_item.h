#pragma once
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/code_item_metadata.h"
#include <cstring>
namespace artdcevm::receiver_guard {
// Fixed Standard/Compact source CodeItem declarations; a bounded immutable
// mapping and independently calibrated ArtMethod layout are mandatory.
inline bool ReadInvokeClassIndex(const evolution::MappedDexMetadata&map,uint32_t method,uint16_t*out)noexcept {
 if(!out||!map.begin||map.size<112||(std::memcmp(map.begin,"dex\n",4)&&std::memcmp(map.begin,"cdex",4)))return false;
 auto u32=[&](size_t at){uint32_t n;std::memcpy(&n,map.begin+at,4);return n;};
 const auto methods=u32(88),method_offset=u32(92),types=u32(64),type_offset=u32(68);
 if(method>=methods||method_offset>map.size||uint64_t(methods)*8>map.size-method_offset||type_offset>map.size||uint64_t(types)*4>map.size-type_offset)return false;
 uint16_t index=0;std::memcpy(&index,map.begin+method_offset+size_t(method)*8,2);if(index>=types)return false;*out=index;return true;
}
inline bool ReadCodeView(const code_item_metadata::Layout&layout,const void*method,size_t method_extent,const evolution::MappedDexMetadata&map,CodeView*out)noexcept {
 if(!out||!map.begin||map.size<112||(std::memcmp(map.begin,"dex\n",4)&&std::memcmp(map.begin,"cdex",4)))return false;
 method_metadata::Identity identity;if(!method_metadata::ReadIdentity(layout.method,method,method_extent,&identity)||identity.dex_method_index==UINT32_MAX||(identity.access_flags&0x500u))return false;
 auto Range=[](const unsigned char*b,size_t n,uintptr_t p,size_t w){auto start=uintptr_t(b);return b&&n<=UINTPTR_MAX-start&&p>=start&&p-start<=n&&w<=n-(p-start);};
 auto U16=[](uintptr_t p){uint16_t n;std::memcpy(&n,reinterpret_cast<const void*>(p),2);return n;};
 auto U32=[](uintptr_t p){uint32_t n;std::memcpy(&n,reinterpret_cast<const void*>(p),4);return n;};
 const uint32_t count=U32(uintptr_t(map.begin)+88),offset=U32(uintptr_t(map.begin)+92);
 if(offset>map.size||uint64_t(count)*8>map.size-offset)return false;
 uint32_t registers=0;if(!code_item_metadata::ReadRegisters(layout,method,method_extent,map,&registers))return false;
 uintptr_t code=0;bool compact=false;if(layout.storage==code_item_metadata::Storage::DexOffset){if(std::memcmp(map.begin,"dex\n",4)&&std::memcmp(map.begin,"cdex",4))return false;compact=!std::memcmp(map.begin,"cdex",4);uint32_t off=0;std::memcpy(&off,static_cast<const char*>(method)+offsetof(method_metadata::Method11,dex_code_item_offset),4);if(!off||off>UINTPTR_MAX-uintptr_t(map.data_begin))return false;code=uintptr_t(map.data_begin)+off;}else{uint64_t pointer=0;std::memcpy(&pointer,static_cast<const char*>(method)+layout.method.data,8);compact=layout.storage==code_item_metadata::Storage::TaggedPointer&&(pointer&1);if(layout.storage==code_item_metadata::Storage::StandardPointer&&(pointer&1))return false;code=uintptr_t(pointer)&~uintptr_t{1};}
 const unsigned char*base=map.data_begin;size_t bytes=map.data_size;if(!Range(base,bytes,code,compact?4:16)){base=map.begin;bytes=map.size;}if(!code||!Range(base,bytes,code,compact?4:16))return false;
 uint32_t units=0;uintptr_t instructions=code+(compact?4:16);if(compact){auto flags=U16(code+2);units=flags>>5;if(flags&16){if(code<4||!Range(base,bytes,code-4,4))return false;uint64_t extended=uint64_t(U16(code-2))+(uint64_t(U16(code-4))<<16)+units;if(extended>UINT32_MAX)return false;units=uint32_t(extended);}}else units=U32(code+12);
 if(!units||uint64_t(units)*2>SIZE_MAX||!Range(base,bytes,instructions,size_t(units)*2)||identity.dex_method_index>=U32(uintptr_t(map.begin)+88))return false;
 // Call-site IDs live in the DEX map list; ordinary invocation parsing needs
 // only method IDs. Custom/polymorphic remain closed until typed bridge proof.
 uint32_t incoming=0,tries=0;
 if(!compact){incoming=U16(code+2);tries=U16(code+6);}
 else {
  const auto fields=U16(code),flags=U16(code+2);incoming=(fields>>8)&15;tries=fields&15;
  uintptr_t pre=code;
  if(flags&16){if(pre<4||!Range(base,bytes,pre-4,4))return false;pre-=4;}
  for(unsigned bit=0;bit<4;++bit)if(flags&(1u<<bit)){
   if(pre<2||!Range(base,bytes,pre-2,2))return false;pre-=2;const auto extension=U16(pre);
   if(bit==1)incoming+=extension;else if(bit==3)tries+=extension;
  }
 }
 if(incoming>registers||incoming>UINT16_MAX||tries>UINT16_MAX)return false;
 *out={reinterpret_cast<const uint16_t*>(instructions),units,registers,U32(uintptr_t(map.begin)+88),0,map,reinterpret_cast<const void*>(code),incoming,tries,compact};return true;
}
}

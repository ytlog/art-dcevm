#pragma once
#include "platform/common/art_method_metadata.h"
#include "evolution/class_schema.h"
namespace artdcevm::code_item_metadata {
// Application runtime (not dex2oat), non-proxy Java methods only. Provider
// maintains calibrated ART declarations, live immutable DEX mappings and
// stable method data under mutator/transaction access. This is not a verifier.
enum class Storage { DexOffset, TaggedPointer, StandardPointer };
struct Layout {method_metadata::Layout method;Storage storage;};
inline constexpr Layout kOffset{method_metadata::kAndroid11,Storage::DexOffset};
inline constexpr Layout kTagged{method_metadata::kAndroid12Onward,Storage::TaggedPointer};
inline constexpr Layout kStandard{method_metadata::kAndroid12Onward,Storage::StandardPointer};
inline bool ReadRegisters(const Layout& layout,const void* method,size_t extent,
                          const evolution::MappedDexMetadata& map,uint32_t* output) {
  method_metadata::Identity identity;
  if(!output || !method_metadata::ReadIdentity(layout.method,method,extent,&identity) ||
     identity.dex_method_index==UINT32_MAX || (identity.access_flags&(0x100u|0x400u)))return false;
  auto range=[](const unsigned char* base,size_t size,uintptr_t pointer,size_t width) {
    const auto start=reinterpret_cast<uintptr_t>(base);
    return base && size<=UINTPTR_MAX-start && pointer>=start && pointer-start<=size && width<=size-(pointer-start);
  };
  auto read16=[](uintptr_t p){uint16_t value;std::memcpy(&value,reinterpret_cast<const void*>(p),2);return value;};
  uintptr_t code;bool compact;
  if(layout.storage==Storage::DexOffset) {
    if(layout.method.size<offsetof(method_metadata::Method11,dex_code_item_offset)+4)return false;
    if(!range(map.begin,map.size,reinterpret_cast<uintptr_t>(map.begin),8))return false;
    compact=std::memcmp(map.begin,"cdex",4)==0;
    if(!compact && std::memcmp(map.begin,"dex\n",4)!=0)return false;
    uint32_t offset;std::memcpy(&offset,static_cast<const unsigned char*>(method)+offsetof(method_metadata::Method11,dex_code_item_offset),4);
    const auto data=reinterpret_cast<uintptr_t>(map.data_begin);
    if(!offset || !data || offset>UINTPTR_MAX-data)return false;
    code=data+offset;
  } else if(layout.storage==Storage::TaggedPointer || layout.storage==Storage::StandardPointer) {
    if(layout.method.size<8 || layout.method.data>layout.method.size-8)return false;
    uint64_t data;std::memcpy(&data,static_cast<const unsigned char*>(method)+layout.method.data,8);
    compact=layout.storage==Storage::TaggedPointer && (data&1);
    code=static_cast<uintptr_t>(data)&~uintptr_t{1};
    // Android16+ stores an untagged StandardDex code pointer.
    if(layout.storage==Storage::StandardPointer && (data&1))return false;
  }
  else return false;
  if(!code || code%(compact?2:4))return false;
  const unsigned char* base=map.data_begin;size_t size=map.data_size;
  if(!range(base,size,code,compact?4:16)){base=map.begin;size=map.size;}
  if(!range(base,size,code,compact?4:16))return false;
  uint32_t count;
  if(!compact)count=read16(code);
  else {
    const auto fields=read16(code),flags=read16(code+2);
    uint32_t regs=(fields>>12)&15,ins=(fields>>8)&15;
    uintptr_t pre=code;
    if(flags&16) {if(pre<4 || !range(base,size,pre-4,4))return false;pre-=4;}
    for(unsigned flag=0;flag<2;++flag)if(flags&(1u<<flag)) {
      if(pre<2 || !range(base,size,pre-2,2))return false;
      pre-=2;const uint16_t extension=read16(pre);
      if(flag==0)regs+=extension;else ins+=extension;
    }
    if(regs+ins>UINT16_MAX)return false;
    count=regs+ins;
  }
  *output=count;return true;
}
}

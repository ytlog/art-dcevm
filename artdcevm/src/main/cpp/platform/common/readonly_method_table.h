#pragma once
#include "platform/common/method_identity_reader.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>
namespace artdcevm::readonly_method_table {
struct Oracle {const void* method;size_t extent;uint32_t index,flags;};
struct Declaration {size_t stride,index_offset,alignment;};
struct Proof {size_t stride=0;uint32_t declared=0,direct=0,total=0;};
// ABI-neutral identity/table algorithm. The caller owns every reflected root
// and runs inside the real ART callback; pointer/extent are observations, and
// DEX method_ids/class_data plus reflection supply independent complete oracle.
// No candidate method/table field may generate its own index or expected count.
inline bool Prove(const Declaration& source,const void* array,size_t extent,
    const std::vector<Oracle>& oracles,uint32_t declared,uint32_t direct,Proof* output,std::string* error) {
  auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!output||!array||oracles.size()<3||oracles.size()>UINT32_MAX||declared!=oracles.size()||direct>declared||
      (source.alignment!=4&&source.alignment!=8)||source.stride%source.alignment||
      source.stride<8||source.index_offset>source.stride-4||source.index_offset%4||
      uintptr_t(array)%source.alignment||uintptr_t(array)>UINTPTR_MAX-source.alignment||extent<source.alignment)
    return fail("Invalid complete method table request");
  uint32_t total;std::memcpy(&total,array,4);
  if(total<oracles.size()||total>(extent-source.alignment)/source.stride||
      total>(UINTPTR_MAX-uintptr_t(array)-source.alignment)/source.stride)
    return fail("Method array exceeds mapped extent or complete DEX declarations");
  std::set<uint32_t> dex,physical;uint32_t actual_direct=0;
  const uintptr_t begin=uintptr_t(array)+source.alignment;
  for(const auto& oracle:oracles) {
    const uintptr_t address=uintptr_t(oracle.method);
    if(address<begin||(address-begin)%source.stride||
        (address-begin)/source.stride>=oracles.size()||
        !physical.insert(uint32_t((address-begin)/source.stride)).second||
        oracle.index==UINT32_MAX||!dex.insert(oracle.index).second)
      return fail("Original reflected leaf does not cover each declared table slot once");
    method_identity_reader::Identity identity{};
    if(!method_identity_reader::ReadIdentity(source.stride,source.index_offset,
        oracle.method,oracle.extent,&identity)||identity.dex_method_index!=oracle.index||
        (identity.access_flags&0x11fu)!=(oracle.flags&0x11fu))
      return fail("Native method identity disagrees with independent APK/reflection oracle");
    actual_direct+=(identity.access_flags&(0x8u|0x2u|0x10000u))!=0;
  }
  if(actual_direct!=direct)return fail("Native direct/virtual classification disagrees with class_data");
  *output={source.stride,uint32_t(oracles.size()),actual_direct,total};
  if(error)error->clear();return true;
}
}

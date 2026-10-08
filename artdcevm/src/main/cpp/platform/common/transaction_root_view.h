#pragma once
#include "platform/abi/arm64/android11/transaction_roots.h"
#include "platform/abi/arm64/android11/class_redefinition.h"
#include "platform/abi/arm64/android13/class_redefinition.h"
#include "platform/abi/arm64/android15/class_redefinition.h"
#include <array>
#include <cstring>
#include <limits>
namespace artdcevm::transaction_root {
using Holder=abi::arm64::android11::TransactionHolder;
using Iterator=abi::arm64::android11::TransactionIterator;
using Bits=abi::arm64::android11::TransactionBits;
using Owners=abi::arm64::android11::TransactionOwners;
enum class OwnerFamily {kAndroid11,kAndroid13,kAndroid15};
struct Metadata {size_t count=0,stride=0;Owners owners{};Bits initialized{},actually_structural{},initial_structural{};};
struct Roots {size_t index=0;void* owner=nullptr;bool initialized=false,actually_structural=false,initial_structural=false;std::array<void*,6> values{};};
inline bool Range(const void* raw,size_t extent,size_t required,size_t alignment) {
  return raw && uintptr_t(raw)%alignment==0 && extent>=required &&
      required-1<=std::numeric_limits<uintptr_t>::max()-uintptr_t(raw);
}
inline bool ReadHolderHeader(const void* raw,size_t extent,Holder* output) {
  if(!output || !Range(raw,extent,sizeof(Holder),alignof(Holder)))return false;
  Holder h;std::memcpy(&h,raw,sizeof(h));
  if(!Range(h.array_handle,sizeof(uint32_t),sizeof(uint32_t),alignof(uint32_t)) ||
     !Range(h.redefinitions,sizeof(Owners),sizeof(Owners),alignof(Owners)))return false;
  *output=h;return true;
}
inline bool ReadIterator(const void* raw,size_t extent,Iterator* output) {
  if(!output || !Range(raw,extent,sizeof(Iterator),alignof(Iterator)))return false;
  Iterator it;std::memcpy(&it,raw,sizeof(it));
  if(it.index<0 || !Range(it.holder,sizeof(Holder),sizeof(Holder),alignof(Holder)))return false;
  *output=it;return true;
}
inline bool BitsValid(Bits bits,size_t count) {
  size_t words=count/64+(count%64!=0);
  return bits.size==count && bits.capacity>=words &&
    (bits.begin ? uintptr_t(bits.begin)%alignof(uint64_t)==0 &&
      bits.capacity<=(std::numeric_limits<uintptr_t>::max()-uintptr_t(bits.begin))/8 : !count&&!bits.capacity);
}
// count is supplied only after the provider validates the live managed root array
// and its 13-slot partition. All native storage remains borrowed and must be mapped,
// immutable, precisely matched and alive under ART mutator access. Arithmetic
// validation cannot establish an allocation's extent or decode compressed roots.
inline bool ReadMetadata(const Holder& holder,size_t count,OwnerFamily family,Metadata* output) {
  if(!output || !Range(holder.redefinitions,sizeof(Owners),sizeof(Owners),alignof(Owners)))return false;
  size_t stride=0;
  switch(family) {
    case OwnerFamily::kAndroid11:stride=sizeof(abi::arm64::android11::ClassRedefinition);break;
    case OwnerFamily::kAndroid13:stride=sizeof(abi::arm64::android13::ClassRedefinition);break;
    case OwnerFamily::kAndroid15:stride=sizeof(abi::arm64::android15::ClassRedefinition);break;
    default:return false;
  }
  Owners owners;std::memcpy(&owners,holder.redefinitions,sizeof(owners));
  uintptr_t begin=uintptr_t(owners.begin),end=uintptr_t(owners.end),capacity=uintptr_t(owners.capacity);
  if((!begin&&(end||capacity)) || (begin&&begin%8) || end<begin || capacity<end ||
     (end-begin)%stride || (capacity-begin)%stride || (end-begin)/stride!=count ||
     !BitsValid(holder.initialized,count) || !BitsValid(holder.actually_structural,count) || !BitsValid(holder.initial_structural,count))return false;
  *output={count,stride,owners,holder.initialized,holder.actually_structural,holder.initial_structural};return true;
}
inline bool Bit(Bits bits,size_t index) {
  uint64_t word;std::memcpy(&word,reinterpret_cast<const unsigned char*>(bits.begin)+8*(index/64),8);
  return (word&(uint64_t{1}<<(index%64)))!=0;
}
// metadata must be the validated snapshot returned by ReadMetadata (or an
// unchanged copy of it). It must not be synthesized from untrusted fields.
// Root callback is the provider's barrier-aware borrowed array access, and must
// not allocate/suspend or modify the holder during this read. Null roots are valid
// before later allocation stages; phase validation remains the owning provider's job.
template<class RootReader> bool ReadRoots(const Metadata& metadata,size_t index,RootReader read,Roots* output) {
  if(!output || index>=metadata.count || !metadata.stride ||
     index>(std::numeric_limits<size_t>::max()/abi::arm64::android11::kRootSlots))return false;
  size_t at=index*abi::arm64::android11::kRootSlots;
  if(at>std::numeric_limits<size_t>::max()-abi::arm64::android11::kNewClassesSlot)return false;
  Roots roots;roots.index=index;roots.owner=const_cast<unsigned char*>(metadata.owners.begin)+metadata.stride*index;
  roots.initialized=Bit(metadata.initialized,index);roots.actually_structural=Bit(metadata.actually_structural,index);roots.initial_structural=Bit(metadata.initial_structural,index);
  constexpr std::array<size_t,6> slots{3,4,9,10,11,12};
  for(size_t i=0;i<slots.size();++i)roots.values[i]=read(at+slots[i]);
  *output=roots;return true;
}
}

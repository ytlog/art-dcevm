#pragma once
#include "platform/abi/arm64/art_field.h"
#include <cstdlib>
#include <cstring>
namespace artdcevm::field_storage {
using Field = abi::arm64::Field;
using ReferenceReader = void* (*)(void*, size_t, bool);
using Barrier = void (*)(void*);
using CurrentThread = void* (*)();
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Field storage requires little endian");
namespace detail {
template<class T> uint64_t Read(void* object,uint32_t offset,bool is_volatile) {
  const auto* address=reinterpret_cast<const T*>(static_cast<unsigned char*>(object)+offset);
  const T value=__atomic_load_n(address,is_volatile?__ATOMIC_SEQ_CST:__ATOMIC_RELAXED);
  uint64_t bits=0;std::memcpy(&bits,&value,sizeof(value));return bits;
}
template<class T> void Write(void* object,uint32_t offset,bool is_volatile,uint64_t bits) {
  T value;std::memcpy(&value,&bits,sizeof(value));
  auto* address=reinterpret_cast<T*>(static_cast<unsigned char*>(object)+offset);
  __atomic_store_n(address,value,is_volatile?__ATOMIC_SEQ_CST:__ATOMIC_RELAXED);
}
}
// Source provider supplies calibrated field offsets and reference/barrier
// operations. Borrowed objects/fields are live under ART's exclusive lock.
inline uint64_t Read(void* object,const Field& field,char type,ReferenceReader reference) {
  const bool vol=(field.access_flags&0x40)!=0;
  switch(type) {
    case 'L':case '[':return reinterpret_cast<uintptr_t>(reference(object,field.offset,vol));
    case 'Z':case 'B':return detail::Read<uint8_t>(object,field.offset,vol);
    case 'C':case 'S':return detail::Read<uint16_t>(object,field.offset,vol);
    case 'I':case 'F':return detail::Read<uint32_t>(object,field.offset,vol);
    case 'J':case 'D':return detail::Read<uint64_t>(object,field.offset,vol);
    default:std::abort();
  }
}
inline void Write(void* object,const Field& field,char type,uint64_t bits,Barrier barrier) {
  const bool vol=(field.access_flags&0x40)!=0;
  switch(type) {
    // Release unpoisoned compressed references must be independently calibrated
    // by the source provider; copying a raw pointer representation is not proof.
    case 'L':case '[':detail::Write<uint32_t>(object,field.offset,vol,bits);if(bits)barrier(object);return;
    case 'Z':case 'B':detail::Write<uint8_t>(object,field.offset,vol,bits);return;
    case 'C':case 'S':detail::Write<uint16_t>(object,field.offset,vol,bits);return;
    case 'I':case 'F':detail::Write<uint32_t>(object,field.offset,vol,bits);return;
    case 'J':case 'D':detail::Write<uint64_t>(object,field.offset,vol,bits);return;
    default:std::abort();
  }
}
inline void TransferMonitor(void* target,void* source,size_t monitor_offset,uint32_t default_word) {
  const uint64_t word=detail::Read<uint32_t>(source,static_cast<uint32_t>(monitor_offset),false);
  detail::Write<uint32_t>(target,static_cast<uint32_t>(monitor_offset),false,word);
  detail::Write<uint32_t>(source,static_cast<uint32_t>(monitor_offset),false,default_word);
}
inline void MarkCard(void* object,CurrentThread current,size_t card_offset,size_t shift,uint8_t dirty) {
  uintptr_t biased;
  std::memcpy(&biased,static_cast<unsigned char*>(current())+card_offset,sizeof(biased));
  auto* card=reinterpret_cast<uint8_t*>(biased+(reinterpret_cast<uintptr_t>(object)>>shift));
  __atomic_store_n(card,dirty,__ATOMIC_RELAXED);
}
}

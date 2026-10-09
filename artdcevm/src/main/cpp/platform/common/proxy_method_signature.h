#pragma once
#include "platform/common/art_method_metadata.h"
#include <map>
#include <string_view>

namespace artdcevm::proxy_method_signature {
// Entries are admitted only after native arena membership, declaring Class and
// independent public declaration have been proved. This map is not a symbol or
// address lookup fallback.
struct Declaration {
 std::string_view name, descriptor;
 bool proxy = false;
};
struct Witness {
 const uintptr_t* slot = nullptr;
 void* source = nullptr;
 bool Unchanged() const noexcept {
  return slot && source && __atomic_load_n(slot,__ATOMIC_RELAXED)==reinterpret_cast<uintptr_t>(source);
 }
};
// AOSP GetMethodName normalizes every method of a proxy Class, including its
// constructor, through ArtMethod::data_. The caller has already proved the
// complete raw ArtMethod mapping and independently classified its Class.
inline bool Resolve(const method_metadata::Layout& layout, void* raw,
 std::string_view name, std::string_view descriptor,
 const std::map<void*,Declaration>& arena, Witness* witness) noexcept {
 if(!raw||!witness||reinterpret_cast<uintptr_t>(raw)%alignof(uintptr_t)||
    layout.data>layout.size||layout.size-layout.data<sizeof(uintptr_t))return false;
 const auto own=arena.find(raw);
 if(own==arena.end()||!own->second.proxy||own->second.name!=name||own->second.descriptor!=descriptor)return false;
 const auto* slot=reinterpret_cast<const uintptr_t*>(static_cast<const char*>(raw)+layout.data);
 void* source=reinterpret_cast<void*>(__atomic_load_n(slot,__ATOMIC_RELAXED));
 const auto original=arena.find(source);
 if(!source||reinterpret_cast<uintptr_t>(source)%alignof(uintptr_t)||original==arena.end()||
    original->second.proxy||original->second.name!=name||original->second.descriptor!=descriptor)return false;
 *witness={slot,source};return true;
}
}

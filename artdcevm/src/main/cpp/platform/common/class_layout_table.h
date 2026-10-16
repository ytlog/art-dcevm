#pragma once
#include "platform/common/runtime_thread.h"
#include <jni.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/runtime_objects.h"
// Source-declared class layouts; runtime probes select among them.
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android12/class.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/android14/class.h"
#include "platform/abi/arm64/android15/class.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android17/class.h"

#include "platform/common/class_layout.h"

// The table of source-declared class layouts, shared by the layout probes
// (class_layout.cc) and the member mapping plans (member_mapping.cc). Both
// build their per-layout operations from Table, so an index names the same
// layout in either.
namespace artdcevm::class_layout {

inline uint32_t Reference(const void* object) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
}
template <typename T> T* Pointer(uint64_t address) {
  return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
template <typename T> T* Heap(uint32_t reference) {
  return static_cast<T*>(runtime_thread::Read(reference));
}
template <typename T> T* Element(const abi::arm64::ObjectArray* array, int32_t index) {
  return Heap<T>(array->elements[index]);
}
template <typename T> abi::arm64::LengthPrefixedArray<T>* Members(uint64_t address) {
  return Pointer<abi::arm64::LengthPrefixedArray<T>>(address);
}
inline const abi::arm64::DexFilePrefix* DexOf(uint32_t dex_cache) {
  const auto* cache = Heap<abi::arm64::DexCache>(dex_cache);
  return cache ? Pointer<const abi::arm64::DexFilePrefix>(cache->dex_file) : nullptr;
}
inline bool Fail(std::string* why, std::string reason) {
  *why = std::move(reason);
  return false;
}

using MirrorClass = void* (*)(abi::arm64::ClassRedefinitionPrefix*);
// GetMirrorClass reads only driver_->self_ and klass_ (ti_redefine.cc, 11--17),
// so it decodes any class reference in a redefinition's thread.
inline void* DecodeClass(MirrorClass mirror_class, abi::arm64::ClassRedefinitionPrefix* self, jclass klass) {
  abi::arm64::ClassRedefinitionPrefix lookup{self->driver, klass, nullptr};
  return mirror_class(&lookup);
}

// The native member arrays of one source-declared class layout.
template <typename Class, typename Method> struct View {
  static auto FieldArrays(Class& klass) {
    const auto slots = FieldArraySlots(klass);
    std::array<uint64_t, std::tuple_size_v<decltype(slots)>> arrays{};
    for (size_t i = 0; i < slots.size(); ++i) arrays[i] = *slots[i];
    return arrays;
  }
  static uint64_t* FieldSlot(void* klass, size_t slot) {
    return FieldArraySlots(*static_cast<Class*>(klass))[slot];
  }
  static uint32_t* Superclass(void* klass) { return &static_cast<Class*>(klass)->super_class; }
};

constexpr size_t kLayoutCount = 7;
using LayoutMask = uint32_t;
static_assert(kLayoutCount <= 32);

// One entry per AOSP release whose declarations are in platform/abi/arm64:
// Ops<Class, Method>::Make(release) for each, in release order.
template <template <typename, typename> class Ops, typename Entry>
constexpr std::array<Entry, kLayoutCount> Table() {
  return {{Ops<abi::arm64::android11::Class, abi::arm64::android11::ArtMethod>::Make(11),
           Ops<abi::arm64::android12::Class, abi::arm64::ArtMethod>::Make(12),
           Ops<abi::arm64::android13::Class, abi::arm64::ArtMethod>::Make(13),
           Ops<abi::arm64::android14::Class, abi::arm64::ArtMethod>::Make(14),
           Ops<abi::arm64::android15::Class, abi::arm64::ArtMethod>::Make(15),
           Ops<abi::arm64::android16::Class, abi::arm64::ArtMethod>::Make(16),
           Ops<abi::arm64::android17::Class, abi::arm64::ArtMethod>::Make(17)}};
}
}  // namespace artdcevm::class_layout

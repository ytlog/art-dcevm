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
#include "platform/abi/arm64/class_layout_table.h"

#include "platform/common/class_layout.h"

// The table of source-declared class layouts, shared by the layout probes
// (class_layout.cc) and the member mapping plans (member_mapping.cc). Both
// build their per-layout operations from Table, so an index names the same
// layout in either.
namespace artdcevm::class_layout {
using runtime_thread::VerifiedThread;

inline uint32_t Reference(const void* object) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
}
template <typename T> T* Pointer(uint64_t address) {
  return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
template <typename T> T* Heap(const VerifiedThread& thread, uint32_t reference) {
  return static_cast<T*>(runtime_thread::Read(thread, reference));
}
template <typename T> T* Element(const VerifiedThread& thread, const abi::arm64::ObjectArray* array, int32_t index) {
  return Heap<T>(thread, array->elements[index]);
}
template <typename T> abi::arm64::LengthPrefixedArray<T>* Members(uint64_t address) {
  return Pointer<abi::arm64::LengthPrefixedArray<T>>(address);
}
inline const abi::arm64::DexFilePrefix* DexOf(const VerifiedThread& thread, uint32_t dex_cache) {
  const auto* cache = Heap<abi::arm64::DexCache>(thread, dex_cache);
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

using abi::arm64::kLayoutCount;
using abi::arm64::LayoutMask;
using abi::arm64::Table;
}  // namespace artdcevm::class_layout

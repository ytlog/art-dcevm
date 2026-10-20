#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Fixed release arm64 runtime declarations identical in the AOSP sources
// android-11.0.0_r1 through android-17.0.0_r1 unless noted. The default
// build has ART_HEAP_POISONING=false, so heap references are 32-bit addresses.
// Uses that depend on a declaration are enabled only after the runtime probes
// in platform/common/class_layout.cc confirm it against ART's own services.
namespace artdcevm::abi::arm64 {

// mirror/object.h: klass_, monitor_.
struct Object {
  uint32_t klass;
  uint32_t monitor;
};
// mirror/array.h + object_array.h: length_ then 32-bit references.
struct ObjectArray {
  Object object;
  int32_t length;
  uint32_t elements[1];
  template <typename T> T* Get(int32_t index) const {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(elements[index]));
  }
};
static_assert(offsetof(ObjectArray, elements) == 12);

// mirror/array.h (android-13.0.0_r1 and android-16.0.0_r1):
// PointerArray uses Array::DataOffset(8) and 64-bit elements on arm64.
// IfTable is ObjectArray<Object>, storing (interface, PointerArray) pairs.
struct PointerArray {
  Object object;
  int32_t length;
  uint32_t padding;
  uint64_t elements[1];
};
static_assert(offsetof(PointerArray, elements) == 16);

// mirror/dex_cache.h: class_loader_, location_, then the native DexFile*.
struct DexCache {
  Object object;
  uint32_t class_loader;
  uint32_t location;
  uint64_t dex_file;
};
static_assert(offsetof(DexCache, dex_file) == 16);

// art_field.h: declaring_class_, access_flags_, field_dex_idx_, offset_.
struct ArtField {
  uint32_t declaring_class;
  uint32_t access_flags;
  uint32_t dex_field_index;
  uint32_t offset;
};
static_assert(sizeof(ArtField) == 16);

// art_method.h: declaring_class_ and access_flags_ lead ArtMethod in 11--17.
struct ArtMethodPrefix {
  uint32_t declaring_class;
  std::atomic<uint32_t> access_flags;
};

// art_method.h of android-12.0.0_r1 through android-17.0.0_r1; Android 11
// declares its own in android11/art_method.h.
struct ArtMethod {
  static constexpr uint32_t kPreCompiled = 0x00800000;  // modifiers.h, Android 12--17.
  uint32_t declaring_class;
  std::atomic<uint32_t> access_flags;
  uint32_t dex_method_index;
  uint16_t method_index;
  uint16_t hotness_count;
  void* data;
  void* quick_entry;
};
inline void ClearCode(ArtMethod*) {}  // Since 12 the code item is stored in data_.
static_assert(sizeof(ArtMethod) == 32);
// libdexfile/dex/modifiers.h; ART sets it on each method of a replaced class.
inline constexpr uint32_t kAccObsoleteMethod = 0x00040000;
inline constexpr uint32_t kAccCompileDontBother = 0x02000000;
inline constexpr uint32_t kAccJavaFlagsMask = 0xffff;

// base/length_prefixed_array.h: At() aligns the first element after length_.
template <typename T> struct LengthPrefixedArray {
  uint32_t length;
  T* At(uint32_t index) {
    constexpr size_t alignment = alignof(T);
    constexpr size_t start = (sizeof(length) + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) + start) + index;
  }
  bool Contains(const T* element) {
    return length && element >= At(0) && element < At(length) &&
        (reinterpret_cast<uintptr_t>(element) - reinterpret_cast<uintptr_t>(At(0))) % sizeof(T) == 0;
  }
};

// interpreter/{shadow_frame,interpreter_switch_impl}.h: identical native
// prefixes in Android 11--17, independent of the trailing frame/context fields.
struct ShadowFramePrefix { void* link; void* method; };
struct SwitchContextPrefix { void* self; const void* accessor; void* frame; };
static_assert(offsetof(ShadowFramePrefix, method) == 8);
static_assert(offsetof(SwitchContextPrefix, frame) == 16);

// handle.h: Handle<T> points at a StackReference<T> (an unpoisoned reference).
struct Handle {
  const uint32_t* reference;
  template <typename T> T* Get() const {
    return reference ? reinterpret_cast<T*>(static_cast<uintptr_t>(*reference)) : nullptr;
  }
};
static_assert(sizeof(Handle) == 8);
}  // namespace artdcevm::abi::arm64

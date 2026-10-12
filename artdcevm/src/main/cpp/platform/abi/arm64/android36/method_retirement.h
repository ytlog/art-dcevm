#pragma once
#include "platform/abi/arm64/common/redefinition.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

// Fixed release arm64 declarations from AOSP ART android16-release,
// ed6c006bd06ae060bd9698fd2cb25c4865512ec3. MethodRetirementAbi below names the
// audited libraries these declarations were checked against; the shared
// method-retirement backend is instantiated with it. No address or member
// layout is inferred from machine code.
namespace artdcevm::abi::arm64::android36 {

struct Object {
  uint32_t klass;
  uint32_t monitor;
};
struct ObjectArray {
  Object object;
  int32_t length;
  uint32_t elements[1];
  template <typename T> T* Get(int32_t index) const {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(elements[index]));
  }
};
static_assert(offsetof(ObjectArray, elements) == 12);

// mirror/class.h: Class's inherited Object and fields through methods_.
struct Class {
  Object object;
  uint32_t class_loader;
  uint32_t component_type;
  uint32_t dex_cache;
  uint32_t ext_data;
  uint32_t iftable;
  uint32_t name;
  uint32_t super_class;
  uint32_t vtable;
  uint64_t fields;
  uint64_t methods;
};
static_assert(offsetof(Class, fields) == 40 && offsetof(Class, methods) == 48);

// mirror/dex_cache.h. HeapReference reads here occur only in ART's exclusive
// mutator-lock commit, with GC stopped by the original transaction. This fixed
// release declaration uses AOSP's default ART_HEAP_POISONING=false build.
struct DexCache {
  Object object;
  uint32_t class_loader;
  uint32_t location;
  uint64_t dex_file;
};
static_assert(offsetof(DexCache, dex_file) == 16);

// art_field.h; art_method.h; LengthPrefixedArray::At aligns the first item.
struct ArtField {
  uint32_t declaring_class;
  uint32_t access_flags;
  uint32_t dex_field_index;
  uint32_t offset;
};
struct ArtMethod {
  uint32_t declaring_class;
  std::atomic<uint32_t> access_flags;
  uint32_t dex_method_index;
  uint16_t method_index;
  uint16_t hotness_count;
  void* data;
  void* quick_entry;
};
static_assert(sizeof(ArtField) == 16 && sizeof(ArtMethod) == 32);
template <typename T> struct LengthPrefixedArray {
  uint32_t length;
  T* At(uint32_t index) {
    constexpr size_t alignment = alignof(T);
    constexpr size_t start = (sizeof(length) + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) + start) + index;
  }
};

// Handle<T> is a pointer to an unpoisoned StackReference<T>. Holder's first
// member is its one handle; Iter contains idx_ followed by Holder&.
struct Handle {
  const uint32_t* reference;
  template <typename T> T* Get() const {
    return reference ? reinterpret_cast<T*>(static_cast<uintptr_t>(*reference)) : nullptr;
  }
};
struct RedefinitionDataHolderPrefix { Handle array; };
struct RedefinitionDataIter {
  int32_t index;
  RedefinitionDataHolderPrefix* holder;
  ObjectArray* Classes(bool old) const {
    constexpr int32_t kNumSlots = 13;
    constexpr int32_t kSlotOldClasses = 11;
    constexpr int32_t kSlotNewClasses = 12;
    return holder->array.Get<ObjectArray>()->Get<ObjectArray>(
        kNumSlots * index + (old ? kSlotOldClasses : kSlotNewClasses));
  }
};
static_assert(sizeof(Handle) == 8 && offsetof(RedefinitionDataIter, holder) == 8);

// scoped_thread_state_change.h: ScopedObjectAccessAlreadyRunnable.
struct ScopedObjectAccess {
  void* self;
  void* env;
  void* vm;
};

// mirror/method_handle_impl.h: only native target and kind are read while the
// caller owns a live ART Handle. Reference fields need no raw dereference.
struct MethodHandle {
  Object object;
  uint32_t as_type_cache;
  uint32_t cached_spread_invoker;
  uint32_t method_type;
  uint32_t handle_kind;
  uint64_t art_field_or_method;
};
static_assert(offsetof(MethodHandle, handle_kind) == 20 &&
              offsetof(MethodHandle, art_field_or_method) == 24);

// reflective_value_visitor.h: destructor, VisitMethod, VisitField in that order.
struct ReflectionSourceInfo {
  const void* vtable;
  int32_t type;
};
struct JniMethodIdSourceInfo {
  ReflectionSourceInfo base;
  jmethodID id;
};
static_assert(offsetof(JniMethodIdSourceInfo, id) == 16);
class ReflectiveValueVisitor {
 public:
  virtual ~ReflectiveValueVisitor() = default;
  virtual ArtMethod* VisitMethod(ArtMethod*, const ReflectionSourceInfo&) = 0;
  virtual ArtField* VisitField(ArtField*, const ReflectionSourceInfo&) = 0;
};

// ART uses LLVM libc++ ABI v1 (std::__1); NDK r27 uses the same concrete
// __tree layout with its std::__ndk1 namespace. The namespace changes symbols,
// not this specialization's object/node layout or allocator ownership. The
// original ART maps are empty and destroyed by ART after this bridge inserts.
// This is deliberately limited to pointer/pointer maps, not general containers.
// ART libc++ source: external/libcxx ff708ee052c18bd192bbb0acb890b7e48718d2a6,
// include/__tree:706--751,1026--1028. Runtime Compatible() checks the compiled
// NDK map's sentinel, root, size and real allocated node before enabling hooks.
#if defined(__ANDROID__)
static_assert(_LIBCPP_ABI_VERSION == 1, "ART map bridge requires libc++ ABI v1");
template <typename T> struct LibcppV1PointerMap {
  using Map = std::map<T*, T*>;
  struct Layout { void* begin; void* root; size_t size; };
  struct NodeLayout {
    void* left;
    void* right;
    void* parent;
    bool black;
    T* key;
    T* value;
  };
  static_assert(sizeof(Map) == sizeof(Layout) && sizeof(Map) == 24);
  static_assert(offsetof(NodeLayout, key) == 32 && sizeof(NodeLayout) == 48);
  using NativeNode = std::__tree_node<std::__value_type<T*, T*>, void*>;
  static_assert(sizeof(NativeNode) == sizeof(NodeLayout));
  static_assert(sizeof(std::__tree_node_base<void*>) == offsetof(NodeLayout, key));
  static bool Compatible() {
    Map specimen;
    Layout empty{};
    std::memcpy(&empty, &specimen, sizeof(empty));
    if (empty.root || empty.size || empty.begin !=
        reinterpret_cast<uint8_t*>(&specimen) + offsetof(Layout, root)) return false;
    T* key = reinterpret_cast<T*>(&specimen);
    specimen[key] = key;
    Layout nonempty{};
    std::memcpy(&nonempty, &specimen, sizeof(nonempty));
    auto* node = static_cast<NativeNode*>(nonempty.root);
    return nonempty.size == 1 && nonempty.begin == nonempty.root && node &&
        reinterpret_cast<uint8_t*>(&node->__value_) - reinterpret_cast<uint8_t*>(node) ==
            offsetof(NodeLayout, key) &&
        node->__value_.__get_value().first == key && node->__value_.__get_value().second == key;
  }
  static void Set(void* map, T* from, T* to) {
    (*static_cast<Map*>(map))[from] = to;
  }
};
#endif

// Declaration profile consumed by platform/common/method_retirement.cc.
struct MethodRetirementAbi {
  using Class = android36::Class;
  using DexCache = android36::DexCache;
  using ArtField = android36::ArtField;
  using ArtMethod = android36::ArtMethod;
  template <typename T> using LengthPrefixedArray = android36::LengthPrefixedArray<T>;
  using Handle = android36::Handle;
  using RedefinitionDataIter = android36::RedefinitionDataIter;
  using ScopedObjectAccess = android36::ScopedObjectAccess;
  using MethodHandle = android36::MethodHandle;
  using ReflectionSourceInfo = android36::ReflectionSourceInfo;
  using JniMethodIdSourceInfo = android36::JniMethodIdSourceInfo;
  using ReflectiveValueVisitor = android36::ReflectiveValueVisitor;
  template <typename T> using PointerMap = LibcppV1PointerMap<T>;

  // GNU build IDs of the audited device-01 libart and libopenjdkjvmti; their
  // full SHA-256 values are recorded in docs/VERIFICATION.md.
  static constexpr std::array<unsigned char, 16> kArtBuildId = {
      0xea, 0x55, 0xd4, 0xac, 0x3d, 0xad, 0x73, 0x0a,
      0xfc, 0xc0, 0xea, 0x0e, 0x88, 0x5a, 0xaa, 0x76};
  static constexpr std::array<unsigned char, 16> kJvmtiBuildId = {
      0x63, 0x42, 0x5f, 0xc0, 0x28, 0x7c, 0x8d, 0x7a,
      0x95, 0x16, 0x3b, 0xa0, 0x8a, 0xa8, 0xaf, 0x85};
  // mirror/class.h: enum class PointerSize : uint32_t, k64 = 8.
  static constexpr uint32_t kPointerSize64 = 8;
  // libdexfile/dex/modifiers.h; set by ART on every method of a replaced class.
  static constexpr uint32_t kAccObsoleteMethod = 0x00040000;
  // reflective_value_visitor.h ReflectionSourceType.
  static constexpr int32_t kSourceJniMethodId = 7;
  static constexpr int32_t kSourceDexCacheResolvedMethod = 8;
  // mirror/method_handle_impl.h: kinds before kFirstAccessorKind invoke methods.
  static constexpr uint32_t kFirstAccessorKind = 8;
};
}  // namespace artdcevm::abi::arm64::android36

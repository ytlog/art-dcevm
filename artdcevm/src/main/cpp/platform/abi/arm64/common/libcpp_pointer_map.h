#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

// ART uses LLVM libc++ ABI v1 (std::__1); NDK r27 uses the same concrete
// __tree layout with its std::__ndk1 namespace. The namespace changes symbols,
// not this specialization's object/node layout or allocator ownership. ART's
// mapping maps are empty when handed to the mapping hook and destroyed by ART
// after this bridge inserts. Deliberately limited to pointer/pointer maps.
// ART libc++ source: external/libcxx ff708ee052c18bd192bbb0acb890b7e48718d2a6,
// include/__tree:706--751,1026--1028. Compatible() checks the compiled NDK map;
// Empty() checks each ART map at runtime before the bridge writes to it.
namespace artdcevm::abi::arm64 {
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
  // An empty libc++ tree: no root, size 0, begin pointing at its own end node.
  static bool Empty(const void* map) {
    Layout layout{};
    std::memcpy(&layout, map, sizeof(layout));
    return !layout.root && !layout.size &&
        layout.begin == static_cast<const uint8_t*>(map) + offsetof(Layout, root);
  }
  static bool Compatible() {
    Map specimen;
    if (!Empty(&specimen)) return false;
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
}  // namespace artdcevm::abi::arm64

#pragma once
#include "platform/common/art_method_metadata.h"
#include "platform/common/class_metadata_abi.h"
#include "evolution/class_schema.h"
namespace artdcevm::method_array {
// Calibrated source declarations, arm64 only. Caller keeps the class/array live
// under ART mutator access and supplies a verified mapped array extent.
enum class Kind { Ordinary, Proxy, Array, Primitive };
enum class Order { DirectThenVirtual, ByFlags };
struct Boundaries {
  uint32_t direct_count, declared_count;
  Order order;
};
inline bool ReadBoundaries(int api, const void* klass, size_t extent, uint32_t total,
                           Kind kind, const evolution::MappedDexMemberCounts* dex,
                           Boundaries* output) {
  const auto* layout = class_metadata::Declaration(api);
  const auto address = reinterpret_cast<uintptr_t>(klass);
  if (!layout || !klass || !output || extent < layout->size || address % 4 ||
      address > UINTPTR_MAX - layout->size) return false;
  Boundaries result{};
  if (api < 37) {
    const auto* bytes = static_cast<const unsigned char*>(klass);
    const size_t copied = api == 36 ? offsetof(class_metadata::Class16, copied_methods_offset)
                                   : offsetof(class_metadata::Class11, copied_methods_offset);
    const size_t direct = api == 36 ? offsetof(class_metadata::Class16, virtual_methods_offset)
                                   : offsetof(class_metadata::Class11, virtual_methods_offset);
    uint16_t d, c;
    std::memcpy(&d, bytes + direct, 2); std::memcpy(&c, bytes + copied, 2);
    result = {d, c, Order::DirectThenVirtual};
  } else {
    result.order = Order::ByFlags;
    switch (kind) {
      case Kind::Ordinary:
        if (!dex || dex->direct_methods > total || dex->virtual_methods > total - dex->direct_methods)
          return false;
        result.direct_count = dex->direct_methods;
        result.declared_count = dex->direct_methods + dex->virtual_methods;
        break;
      case Kind::Proxy:
        if (!total) return false;
        result.direct_count = 1; result.declared_count = total;
        break;
      case Kind::Array: case Kind::Primitive: break;
      default: return false;
    }
  }
  if (result.direct_count > result.declared_count || result.declared_count > total) return false;
  *output = result; return true;
}
struct View {
  void* array = nullptr;
  uint32_t count = 0;
  size_t stride = 0;
  Boundaries boundaries{};
  void* At(uint32_t index) const {
    return index < count ? static_cast<unsigned char*>(array) + 8 + size_t(index) * stride : nullptr;
  }
  // Only the old layout represents direct methods as a contiguous prefix.
  // Modern declarations must be filtered by source ArtMethod::IsDirect flags.
  bool IsDirect(uint32_t index) const {
    if (index >= boundaries.declared_count || index >= count) return false;
    return boundaries.order == Order::DirectThenVirtual ? index < boundaries.direct_count
        : (method_metadata::ReadAccessFlags(At(index)) & (0x8u | 0x2u | 0x10000u)) != 0;
  }
};
inline bool Read(int api, void* array, size_t extent, const Boundaries& boundaries, View* output) {
  const auto* layout = method_metadata::Declaration(api);
  if (!layout || !output || (boundaries.order != Order::DirectThenVirtual && boundaries.order != Order::ByFlags) ||
      boundaries.order != (api == 37 ? Order::ByFlags : Order::DirectThenVirtual)) return false;
  const auto address = reinterpret_cast<uintptr_t>(array);
  uint32_t count = 0;
  if (array) {
    if (address % 8 || extent < 8 || address > UINTPTR_MAX - 8) return false;
    std::memcpy(&count, array, 4);
    if (count > (extent - 8) / layout->size || count > (UINTPTR_MAX - address - 8) / layout->size)
      return false;
  } else if (extent) return false;
  if (boundaries.direct_count > boundaries.declared_count || boundaries.declared_count > count) return false;
  *output = {array, count, layout->size, boundaries}; return true;
}
}

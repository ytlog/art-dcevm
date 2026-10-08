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
// Tail boundaries are a distinct source capability from the scalar Class
// prefix. Modern classes derive counts from independent DEX rather than tails.
struct ClassLayout {
  class_metadata::Layout scalar;
  Order order;
  size_t copied_methods, virtual_methods;
};
inline constexpr ClassLayout kLegacy{class_metadata::kLegacy,Order::DirectThenVirtual,
    offsetof(class_metadata::Class11,copied_methods_offset),offsetof(class_metadata::Class11,virtual_methods_offset)};
inline constexpr ClassLayout k16{class_metadata::k16,Order::DirectThenVirtual,
    offsetof(class_metadata::Class16,copied_methods_offset),offsetof(class_metadata::Class16,virtual_methods_offset)};
inline constexpr ClassLayout k17{class_metadata::k17,Order::ByFlags,0,0};
inline bool ReadBoundaries(const ClassLayout& declaration, const void* klass, size_t extent, uint32_t total,
                           Kind kind, const evolution::MappedDexMemberCounts* dex,
                           Boundaries* output) {
  const auto* layout = class_metadata::detail::CheckedDeclaration(declaration.scalar,klass,extent);
  if (!layout || !output) return false;
  Boundaries result{};
  if (declaration.order == Order::DirectThenVirtual) {
    if(layout->size<2 || declaration.copied_methods>layout->size-2 ||
       declaration.virtual_methods>layout->size-2)return false;
    const auto* bytes = static_cast<const unsigned char*>(klass);
    const size_t copied = declaration.copied_methods;
    const size_t direct = declaration.virtual_methods;
    uint16_t d, c;
    std::memcpy(&d, bytes + direct, 2); std::memcpy(&c, bytes + copied, 2);
    result = {d, c, Order::DirectThenVirtual};
  } else if(declaration.order == Order::ByFlags) {
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
  else return false;
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
  // Copied defaults/mirandas remain virtual; only direct declarations are
  // excluded. Modern declarations may be interleaved with virtual methods.
  template<class Fn> bool VisitVirtual(Fn action) const {
    for(uint32_t i=0;i<count;++i)if(!IsDirect(i) && action(At(i)))return true;
    return false;
  }
};
inline bool Read(const method_metadata::Layout& declaration, void* array, size_t extent,
                 const Boundaries& boundaries, View* output) {
  const auto* layout=&declaration;
  if (!output || !layout->size || (boundaries.order != Order::DirectThenVirtual &&
      boundaries.order != Order::ByFlags)) return false;
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
// Trusted ART-owned allocation path, after exact ABI/profile calibration.
// Its length header supplies extent; it cannot prove independent mapped size.
// Caller maintains roots/mutator access; use Read with a verified extent when
// available. Flag-ordered ordinary classes additionally need DEX declaration counts.
inline bool ReadTrustedClass(const ClassLayout& declaration,const method_metadata::Layout& method,const void* klass,size_t extent,Kind kind,
                             const evolution::MappedDexMemberCounts* dex,View* output) {
  const auto* layout=class_metadata::detail::CheckedDeclaration(declaration.scalar,klass,extent);
  if(!layout || !method.size || !output ||
      layout->size<offsetof(class_metadata::Class11,methods)+8)return false;
  uint64_t native;
  std::memcpy(&native,static_cast<const unsigned char*>(klass)+offsetof(class_metadata::Class11,methods),8);
  auto* array=reinterpret_cast<void*>(static_cast<uintptr_t>(native));
  const auto address=reinterpret_cast<uintptr_t>(array);
  if(array && (address%8 || address>UINTPTR_MAX-8))return false;
  uint32_t count=0;
  if(array)std::memcpy(&count,array,4);
  if(array && count>(SIZE_MAX-8)/method.size)return false;
  Boundaries boundaries;
  if(!ReadBoundaries(declaration,klass,extent,count,kind,dex,&boundaries))return false;
  return Read(method,array,array?8+size_t{count}*method.size:0,boundaries,output);
}
}

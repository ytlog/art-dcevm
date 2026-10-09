#pragma once
#include <cstdint>
#include <optional>
#include <type_traits>
namespace artdcevm::abi::arm64::android11::frame_registers {
// Fixed android-11.0.0_r1 runtime/stack.h and dex_register_location.h.
enum Kind:int32_t {kReference=0,kInt=1,kFloat=2,kLongLo=3,kLongHi=4,kDoubleLo=5,kDoubleHi=6};
struct Location {int32_t kind,value;};
static_assert(sizeof(Location)==8 && alignof(Location)==4);
using OptionalLocation=std::optional<Location>;
static_assert(sizeof(OptionalLocation)==12&&alignof(OptionalLocation)==4);
static_assert(std::is_trivially_copy_constructible_v<OptionalLocation>);
using Canonical=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*,OptionalLocation);
using AccessibleRegister=bool (*)(const void*,uint32_t,Kind,uint32_t*);
inline constexpr char kCanonical[]="_ZNK3art12StackVisitor7GetVRegEPNS_9ArtMethodEtNS_8VRegKindEPjNSt3__18optionalINS_19DexRegisterLocationEEE";
inline constexpr char kAccessibleRegister[]="_ZNK3art12StackVisitor23GetRegisterIfAccessibleEjNS_8VRegKindEPj";
// The 11 helper selects core/FPU and the high half from VRegKind, unlike 12+.
inline constexpr Kind KindForLocation(int32_t kind) {
  return kind==3?kLongHi:kind==4?kDoubleLo:kind==5?kDoubleHi:kInt;
}
using Slow=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*);
using Fast=bool (*)(const void*,Location,Kind,uint32_t*);
inline constexpr char kSlow[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeEPNS_9ArtMethodEtNS_8VRegKindEPj";
inline constexpr char kFast[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeENS_19DexRegisterLocationENS_8VRegKindEPj";
}

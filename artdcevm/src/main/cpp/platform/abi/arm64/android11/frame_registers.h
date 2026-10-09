#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::android11::frame_registers {
// Fixed android-11.0.0_r1 runtime/stack.h and dex_register_location.h.
enum Kind:int32_t {kReference=0,kInt=1};
struct Location {int32_t kind,value;};
static_assert(sizeof(Location)==8 && alignof(Location)==4);
using Slow=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*);
using Fast=bool (*)(const void*,Location,Kind,uint32_t*);
inline constexpr char kSlow[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeEPNS_9ArtMethodEtNS_8VRegKindEPj";
inline constexpr char kFast[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeENS_19DexRegisterLocationENS_8VRegKindEPj";
}

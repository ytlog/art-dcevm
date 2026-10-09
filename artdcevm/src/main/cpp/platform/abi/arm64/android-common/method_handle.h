#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace artdcevm::abi::arm64::android_common {
// Fixed mirror/method_handle_impl.h field prefix in the audited ART 11--17
// releases. Runtime use additionally requires independent field-handle kind
// oracles: native target visitor equality alone does not prove this scalar.
#pragma pack(push,4)
struct MethodHandlePrefix {
  uint32_t klass,monitor,as_type_cache,cached_spread_invoker,method_type,kind;
  uint64_t target;
};
#pragma pack(pop)
static_assert(sizeof(MethodHandlePrefix)==32&&offsetof(MethodHandlePrefix,kind)==20);
inline constexpr uint32_t kInstanceGet=8,kInstancePut=9,kStaticGet=10,kStaticPut=11;
inline uint32_t MethodHandleKind(const void* object){
  uint32_t value;std::memcpy(&value,static_cast<const char*>(object)+offsetof(MethodHandlePrefix,kind),sizeof(value));return value;
}
}

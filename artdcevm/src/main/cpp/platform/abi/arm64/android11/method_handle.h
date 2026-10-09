#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace artdcevm::abi::arm64::android11 {
// Fixed Android 11/12 mirror/method_handle_impl.h: nominal_type_ and
// kInvokeCallSiteTransform precede the field accessor enum values.
#pragma pack(push,4)
struct MethodHandlePrefix {
  uint32_t klass,monitor,cached_spread_invoker,nominal_type,method_type,kind;
  uint64_t target;
};
#pragma pack(pop)
static_assert(sizeof(MethodHandlePrefix)==32&&offsetof(MethodHandlePrefix,kind)==20);
inline uint32_t MethodHandleKind(const void* object){
  uint32_t value;std::memcpy(&value,static_cast<const char*>(object)+offsetof(MethodHandlePrefix,kind),sizeof(value));
  // Normalize only the four independently constructed field accessors.
  return value>=9&&value<=12?value-1:UINT32_MAX;
}
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace artdcevm::abi::arm64::android_common {
// Fixed mirror/method_handle_impl.h field prefix in the audited ART releases with as_type_cache_
// releases. Runtime use additionally requires independent field-handle kind
// oracles: native target visitor equality alone does not prove this scalar.
#pragma pack(push,4)
struct MethodHandlePrefix {
  uint32_t klass,monitor,as_type_cache,cached_spread_invoker,method_type,kind;
  uint64_t target;
};
#pragma pack(pop)
static_assert(sizeof(MethodHandlePrefix)==32&&offsetof(MethodHandlePrefix,kind)==20);
// Fixed original mirror/method_type.h declaration. Runtime consumers must
// calibrate these fields against public MethodType parameter Class roots.
struct MethodTypePrefix {
  uint32_t klass,monitor,form,descriptor,parameters,result,wrap_alt;
};
static_assert(sizeof(MethodTypePrefix)==28&&offsetof(MethodTypePrefix,parameters)==16);
inline constexpr uint32_t kInstanceGet=8,kInstancePut=9,kStaticGet=10,kStaticPut=11;
inline uint32_t MethodHandleKind(const void* object){
  uint32_t value;std::memcpy(&value,static_cast<const char*>(object)+offsetof(MethodHandlePrefix,kind),sizeof(value));return value;
}
}

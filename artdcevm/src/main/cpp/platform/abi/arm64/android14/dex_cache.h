#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android14 {
// Fixed source declaration; actual device Profile and live storage are prerequisites.
#pragma pack(push,4)
struct DualCache {
  uint32_t klass,monitor,loader,location;
  uint64_t dex_file,call_sites,fields,fields_array,method_types,method_types_array,
      methods,methods_array,types,types_array,strings,strings_array;
};
#pragma pack(pop)
static_assert(sizeof(DualCache)==112);
static_assert(offsetof(DualCache,methods)==64 && offsetof(DualCache,methods_array)==72);

}

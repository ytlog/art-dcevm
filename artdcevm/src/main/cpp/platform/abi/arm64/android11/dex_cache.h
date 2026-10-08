#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed source declaration; actual device Profile and live storage are prerequisites.
#pragma pack(push,4)
struct LegacyCache {
  uint32_t klass,monitor,loader,location;
  uint64_t dex_file,preresolved_strings,call_sites,fields,method_types,methods,types,strings;
  uint32_t num_preresolved_strings,num_call_sites,num_fields,num_method_types,num_methods,num_types,num_strings;
};
#pragma pack(pop)
static_assert(sizeof(LegacyCache)==108);
static_assert(offsetof(LegacyCache,methods)==56 && offsetof(LegacyCache,num_methods)==96);

}

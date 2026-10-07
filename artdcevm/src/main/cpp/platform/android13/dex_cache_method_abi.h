#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13 {
namespace dex_cache_method_abi {
struct alignas(16) Pair { uintptr_t method; uintptr_t index; };
static_assert(sizeof(Pair)==16 && std::atomic<Pair>::is_always_lock_free);
#pragma pack(push,4)
struct Cache {
  uint32_t klass,monitor,loader,location;
  uint64_t dex_file,preresolved_strings,call_sites,fields,method_types,methods,types,strings;
  uint32_t num_preresolved_strings,num_call_sites,num_fields,num_method_types,num_methods,num_types,num_strings;
};
#pragma pack(pop)
static_assert(offsetof(Cache,methods)==56 && offsetof(Cache,num_methods)==96);
inline constexpr uint32_t kSlots=1024;
// Mirrors DexCache::GetNativePair / GetObjectForIndex. Caller keeps table live.
bool Lookup(const std::atomic<Pair>*,uint32_t count,uint32_t index,void** output);
}
}

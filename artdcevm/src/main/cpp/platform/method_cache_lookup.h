#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::method_cache {
// Fixed arm64 source declarations from android-11/12/12.1/13.0.0_r1.
struct alignas(16) Pair { uintptr_t method; uintptr_t index; };
static_assert(sizeof(Pair)==16 && std::atomic<Pair>::is_always_lock_free);
#pragma pack(push,4)
struct LegacyCache {
  uint32_t klass,monitor,loader,location;
  uint64_t dex_file,preresolved_strings,call_sites,fields,method_types,methods,types,strings;
  uint32_t num_preresolved_strings,num_call_sites,num_fields,num_method_types,num_methods,num_types,num_strings;
};
#pragma pack(pop)
static_assert(offsetof(LegacyCache,methods)==56 && offsetof(LegacyCache,num_methods)==96);
inline constexpr uint32_t kSlots=1024;
// Android 14-17 DexCache::DEFINE_DUAL_CACHE / NativeArray declarations.
#pragma pack(push,4)
struct DualCache {
  uint32_t klass,monitor,loader,location;
  uint64_t dex_file,call_sites,fields,fields_array,method_types,method_types_array,
      methods,methods_array,types,types_array,strings,strings_array;
};
#pragma pack(pop)
static_assert(offsetof(DualCache,methods)==64 && offsetof(DualCache,methods_array)==72);
// Profile/calibration must validate the declarations before reading objects.
// Borrowed native tables stay alive under the caller's ART mutator lock.
bool Lookup(const std::atomic<Pair>*,uint32_t count,uint32_t index,void** output);
// Android 15-17 AtomicPair<uint64_t>: key/version precedes method value.
struct alignas(16) SequencePair { std::atomic<uint64_t> key,method; };
static_assert(sizeof(SequencePair)==16 && offsetof(SequencePair,method)==8);
inline constexpr uint64_t kSequenceMask=0xffffffff00000000ull;
inline constexpr uint64_t kSequenceLock=0x8000000000000000ull;
// method_count comes from the verified DEX NumMethodIds, not a legacy count
// field. An allocated full array wins even when its selected entry is null.
// Read the source-declared cache pointer fields atomically. Legacy counts are
// read after acquiring their array publication; modern method_count is supplied
// by the verified DEX. extent belongs to the live, calibrated managed object.
bool ReadCache(int api,const void* cache,size_t extent,uint32_t method_count,
               uint32_t index,void** output);
bool LookupDual(int api,const std::atomic<uintptr_t>* full,const void* pairs,
                uint32_t method_count,uint32_t index,void** output);
}

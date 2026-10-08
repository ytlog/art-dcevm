#pragma once
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android11/native_cache_pair.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include "platform/abi/arm64/android15/native_cache_pair.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::method_cache {
// Fixed arm64 source declarations from android-11/12/12.1/13.0.0_r1.
using Pair = abi::arm64::android11::Pair;
using LegacyCache = abi::arm64::android11::LegacyCache;
inline constexpr uint32_t kSlots=1024;
// Android 14-17 DexCache::DEFINE_DUAL_CACHE / NativeArray declarations.
using DualCache = abi::arm64::android14::DualCache;
// Profile/calibration must validate the declarations before reading objects.
// Borrowed native tables stay alive under the caller's ART mutator lock.
bool Lookup(const std::atomic<Pair>*,uint32_t count,uint32_t index,void** output);
// Android 15-17 AtomicPair<uint64_t>: key/version precedes method value.
using SequencePair = abi::arm64::android15::SequencePair;
using abi::arm64::android15::kSequenceMask;
using abi::arm64::android15::kSequenceLock;
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

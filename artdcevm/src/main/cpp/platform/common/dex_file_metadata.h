#pragma once
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
namespace artdcevm::dex_file_metadata {
// Caller supplies calibrated, live native/managed object extents and keeps
// the cache rooted under ART mutator access. This immutable native DexFile
// link is not a managed reference and needs no heap-reference decoding.
inline bool ReadFile(int api, const void* dex_file, size_t extent, uint32_t class_index,
                     evolution::MappedDexMetadata* output, std::string* error) {
  auto fail=[&](const char* message){if(error)*error=message;return false;};
  const auto address=reinterpret_cast<uintptr_t>(dex_file);
  if(api<30 || api>37 || !output || !dex_file || address%8 ||
      extent<sizeof(dex_mapping_abi::Prefix) || address>UINTPTR_MAX-sizeof(dex_mapping_abi::Prefix))
    return fail("Invalid native DexFile metadata context");
  dex_mapping_abi::Prefix prefix;
  std::memcpy(&prefix,dex_file,sizeof(prefix));
  return dex_mapping_abi::Normalize(api,prefix,class_index,output,error);
}
inline bool ReadCache(int api, const void* cache, size_t cache_extent, size_t dex_file_extent,
                      uint32_t class_index, evolution::MappedDexMetadata* output, std::string* error) {
  auto fail=[&](const char* message){if(error)*error=message;return false;};
  if(api<30 || api>37 || !output || !cache)return fail("Invalid DexCache mapping context");
  const size_t offset=api<=33 ? offsetof(abi::arm64::android11::LegacyCache,dex_file)
                            : offsetof(abi::arm64::android14::DualCache,dex_file);
  const auto address=reinterpret_cast<uintptr_t>(cache);
  // Only the calibrated immutable prefix through dex_file is read here.
  if(address%4 || cache_extent<offset+8 || address>UINTPTR_MAX-offset-8)
    return fail("Invalid DexCache mapping extent");
  uint64_t native;
  std::memcpy(&native,static_cast<const unsigned char*>(cache)+offset,8);
  if(!native)return fail("Explicit DexCache has no DEX file");
  return ReadFile(api,reinterpret_cast<const void*>(static_cast<uintptr_t>(native)),dex_file_extent,
                  class_index,output,error);
}
}

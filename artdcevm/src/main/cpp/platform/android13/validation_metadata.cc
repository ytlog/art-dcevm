#include "platform/android13/validation_metadata.h"
#include "platform/symbols/android13.h"
#include <cstring>
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
// Release ART ObjPtr<Class> has one uintptr_t, trivial copy/destruction and no
// debug cookie encoding. The transitional probe checks the same fixed profile.
using GetMirrorClass = uintptr_t (*)(void*);
GetMirrorClass get_mirror_class = nullptr;
ProbeValidationMetadata probe_metadata = nullptr;

template<class T>
T Read(const void* object, size_t offset) {
  T value;
  std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
  return value;
}
}

bool ReadValidationMetadata(void* definition, ValidationMetadata* output) {
  using namespace validation_abi;
  auto* klass = reinterpret_cast<const void*>(get_mirror_class(definition));
  if (!klass) return false;
  // Match Class::GetDexFile: it explicitly reads this immutable DexCache link
  // without a read barrier. From-space and to-space caches name the same native
  // DexFile. Heap-reference poisoning is disabled by the verified release profile.
  uint32_t cache_reference = Read<uint32_t>(klass, offsetof(ClassPrefix, dex_cache));
  auto* cache = reinterpret_cast<const void*>(static_cast<uintptr_t>(cache_reference));
  if (!cache) return false;
  auto* dex = reinterpret_cast<const void*>(
      Read<uint64_t>(cache, offsetof(DexCachePrefix, dex_file)));
  if (!dex) return false;
  const auto mapping = Read<DexMappingPrefix>(dex, 0);
  ValidationMetadata metadata{{mapping.begin, mapping.size, mapping.data_begin, mapping.data_size,
      static_cast<uint16_t>(Read<int32_t>(klass, offsetof(ClassPrefix, dex_class_def_index)))},
      Read<uint32_t>(klass, offsetof(ClassPrefix, access_flags))};
  if (!metadata.dex.begin || metadata.dex.size < 0x70 || !metadata.dex.data_begin) return false;
  if (!probe_metadata(definition, &metadata)) return false;
  *output = metadata;
  return true;
}

bool ConfigureValidationMetadata(HookEngine& hooks, void* implementation, std::string* error) {
  using Configure = int (*)();
  auto configure = reinterpret_cast<Configure>(
      dlsym(implementation, "ArtDcevmSourceConfigureValidationMetadata"));
  auto getter = reinterpret_cast<GetMirrorClass>(
      hooks.FindSymbol("libopenjdkjvmti.so", cas::kGetMirrorClass));
  auto probe = reinterpret_cast<ProbeValidationMetadata>(
      dlsym(implementation, "ArtDcevmSourceProbeValidationMetadata"));
  if (!configure || !getter || !probe) {
    *error = "Missing fixed validation-metadata probe or named ART class getter";
    return false;
  }
  get_mirror_class = getter;
  if (configure() != 0) {
    *error = "Fixed validation-metadata declarations did not match the release ART profile";
    return false;
  }
  probe_metadata = probe;
  return true;
}
}

#include "platform/android13/validation_metadata.h"
#include "platform/symbols/android13.h"
#include <cstring>
#include <limits>

namespace artdcevm::android13 {
namespace {
// Release ART ObjPtr<Class> has one uintptr_t, trivial copy/destruction and no
// debug cookie encoding in the matched fixed release profile.
using GetMirrorClass = uintptr_t (*)(void*);
GetMirrorClass get_mirror_class = nullptr;

template<class T>
T Read(const void* object, size_t offset) {
  T value;
  std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
  return value;
}
}

void* GetValidationClass(void* definition) {
  return get_mirror_class ? reinterpret_cast<void*>(get_mirror_class(definition)) : nullptr;
}
bool ReadValidationClassMetadata(void* klass, ValidationMetadata* output) {
  using namespace validation_abi;
  if (!klass || !output) return false;
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
  if (!metadata.dex.begin || metadata.dex.size < 0x70 || !metadata.dex.data_begin ||
      reinterpret_cast<uintptr_t>(metadata.dex.begin) > std::numeric_limits<uintptr_t>::max()-metadata.dex.size ||
      reinterpret_cast<uintptr_t>(metadata.dex.data_begin) > std::numeric_limits<uintptr_t>::max()-metadata.dex.data_size) return false;
  *output = metadata;
  return true;
}
bool ReadValidationMetadata(void* definition, ValidationMetadata* output) {
  ValidationMetadata metadata{};
  if (!output || !ReadValidationClassMetadata(GetValidationClass(definition), &metadata)) return false;
  *output = metadata;
  return true;
}

bool ConfigureValidationMetadata(HookEngine& hooks, void*, std::string* error) {
  auto getter = reinterpret_cast<GetMirrorClass>(hooks.FindSymbol("libopenjdkjvmti.so", cas::kGetMirrorClass));
  if (!getter) { *error = "Missing named ART class getter"; return false; }
  get_mirror_class = getter;return true;
}
}

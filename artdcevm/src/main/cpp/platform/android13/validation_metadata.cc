#include "platform/android13/validation_metadata.h"
#include "platform/symbols/android13.h"
#include <cstring>

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
  class_metadata::Identity identity{};
  if(!class_metadata::ReadIdentity(33,klass,sizeof(ClassPrefix),&identity) || identity.class_def_index<0)return false;
  uint32_t cache_reference = identity.dex_cache;
  auto* cache = reinterpret_cast<const void*>(static_cast<uintptr_t>(cache_reference));
  if (!cache) return false;
  auto* dex = reinterpret_cast<const void*>(
      Read<uint64_t>(cache, offsetof(DexCachePrefix, dex_file)));
  if (!dex) return false;
  const auto mapping = Read<DexMappingPrefix>(dex, 0);
  ValidationMetadata metadata{};
  if(!dex_mapping_abi::Normalize(33,mapping,static_cast<uint32_t>(identity.class_def_index),&metadata.dex,nullptr))return false;
  metadata.access_flags=identity.access_flags;
  *output = metadata;
  return true;
}
bool ReadValidationMetadata(void* definition, ValidationMetadata* output) {
  ValidationMetadata metadata{};
  if (!output || !ReadValidationClassMetadata(GetValidationClass(definition), &metadata)) return false;
  *output = metadata;
  return true;
}

bool ConfigureValidationMetadata(HookEngine& hooks, std::string* error) {
  auto getter = reinterpret_cast<GetMirrorClass>(hooks.FindSymbol("libopenjdkjvmti.so", cas::kGetMirrorClass));
  if (!getter) { *error = "Missing named ART class getter"; return false; }
  get_mirror_class = getter;return true;
}
}

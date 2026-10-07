#include "platform/android13/retired_field_registry_bridge.h"

#include <cstdlib>
#include "platform/android13/retired_field_registry.h"

namespace artdcevm::android13 {
namespace {
RetiredFieldRegistry registry;
RetiredFieldRegistry::ContainsUnsafe contains_unsafe = nullptr;

void Retire(void* field) noexcept {
  try { registry.Retire(field); } catch (...) { std::abort(); }
}
uint32_t TokenFor(void* field) { return registry.TokenFor(field); }
}  // namespace

size_t RetiredFieldSlotCount() { return registry.SlotCount(); }
void RetireFieldPointer(void* member) noexcept { Retire(member); }
uint32_t RetiredFieldTokenFor(void* field) { return TokenFor(field); }
bool IsRetiredFieldPointer(void* field) { return registry.TokenFor(field) != 0; }
void* ResolveRetiredFieldToken(uint32_t token) { return registry.Resolve(token); }

size_t PruneRetiredFields(const void* allocator) noexcept {
  try { return registry.Prune(allocator, contains_unsafe); }
  catch (...) { std::abort(); }
}

bool ConfigureRetiredFieldRegistry(AllocatorContains contains, std::string* error) {
  if (!contains) {
    *error = "Missing calibrated allocator membership for retired-field registry";
    return false;
  }
  contains_unsafe = contains;
  return true;
}

}  // namespace artdcevm::android13

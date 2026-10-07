#include "platform/android13/retired_field_registry_bridge.h"

#include <dlfcn.h>
#include <cstdlib>
#include "platform/android13/retired_field_registry.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
RetiredFieldRegistry registry;
RetiredFieldRegistry::ContainsUnsafe contains_unsafe = nullptr;

void Retire(void* field) noexcept {
  try { registry.Retire(field); } catch (...) { std::abort(); }
}
uint32_t TokenFor(void* field) { return registry.TokenFor(field); }
void* Resolve(uint32_t token) { return registry.Resolve(token); }
size_t Count() { return registry.Count(); }
size_t SlotCount() { return registry.SlotCount(); }
}  // namespace

size_t PruneRetiredFields(const void* allocator) noexcept {
  try { return registry.Prune(allocator, contains_unsafe); }
  catch (...) { std::abort(); }
}

bool ConfigureRetiredFieldRegistry(HookEngine& hooks, void* implementation,
                                   std::string* error) {
  using Configure = void (*)(const RetiredFieldCallbacks*);
  auto configure = reinterpret_cast<Configure>(
      dlsym(implementation, "ArtDcevmSourceConfigureRetiredFields"));
  auto contains = reinterpret_cast<RetiredFieldRegistry::ContainsUnsafe>(
      hooks.FindSymbol("libart.so", cas::kLinearAllocContainsUnsafe));
  if (!configure || !contains) {
    *error = "Missing retired-field registry bridge or named LinearAlloc symbol";
    return false;
  }
  contains_unsafe = contains;
  const RetiredFieldCallbacks callbacks{Retire, TokenFor, Resolve, Count, SlotCount};
  configure(&callbacks);
  return true;
}

}  // namespace artdcevm::android13

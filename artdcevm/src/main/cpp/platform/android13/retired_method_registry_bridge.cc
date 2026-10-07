#include "platform/android13/retired_method_registry_bridge.h"

#include <dlfcn.h>
#include <cstdlib>
#include "platform/android13/retired_method_registry.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
RetiredMethodRegistry registry;
RetiredMethodRegistry::ContainsUnsafe contains_unsafe = nullptr;

void Retire(void* method) noexcept {
  try { registry.Retire(method); } catch (...) { std::abort(); }
}
bool Contains(void* method) { return registry.Contains(method); }
size_t Count() { return registry.Count(); }
size_t Prune(const void* allocator) noexcept {
  try { return registry.Prune(allocator, contains_unsafe); }
  catch (...) { std::abort(); }
}
}  // namespace

bool ConfigureRetiredMethodRegistry(HookEngine& hooks, void* implementation,
                                    std::string* error) {
  using Configure = void (*)(void (*)(void*), bool (*)(void*), size_t (*)(),
                             size_t (*)(const void*));
  auto configure = reinterpret_cast<Configure>(
      dlsym(implementation, "ArtDcevmSourceConfigureRetiredMethods"));
  auto contains = reinterpret_cast<RetiredMethodRegistry::ContainsUnsafe>(
      hooks.FindSymbol("libart.so", cas::kLinearAllocContainsUnsafe));
  if (!configure || !contains) {
    *error = "Missing retired-method registry bridge or named LinearAlloc symbol";
    return false;
  }
  contains_unsafe = contains;
  configure(Retire, Contains, Count, Prune);
  return true;
}

}  // namespace artdcevm::android13

#include "platform/android13/retired_method_registry_bridge.h"

#include <cstdlib>
#include "platform/android13/retired_method_registry.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
RetiredMethodRegistry registry;
RetiredMethodRegistry::ContainsUnsafe contains_unsafe = nullptr;

void Retire(void* method) noexcept {
  try { registry.Retire(method); } catch (...) { std::abort(); }
}
}  // namespace

void RetireMethodPointer(void* member) noexcept { Retire(member); }
bool IsRetiredMethodPointer(void* method) { return registry.Contains(method); }
size_t RetiredMethodCount() { return registry.Count(); }

size_t PruneRetiredMethods(const void* allocator) noexcept {
  try { return registry.Prune(allocator, contains_unsafe); }
  catch (...) { std::abort(); }
}

bool ConfigureRetiredMethodRegistry(HookEngine& hooks, void*,
                                    std::string* error) {
  auto contains = reinterpret_cast<RetiredMethodRegistry::ContainsUnsafe>(
      hooks.FindSymbol("libart.so", cas::kLinearAllocContainsUnsafe));
  if (!contains) {
    *error = "Missing named LinearAlloc symbol for retired-method registry";
    return false;
  }
  RetiredMemberGuardFunctions functions{
      reinterpret_cast<CurrentGuardThread>(hooks.FindSymbol("libart.so", kGuardCurrentThread)),
      reinterpret_cast<ThrowGuardException>(hooks.FindSymbol("libart.so", kGuardThrowNewException)),
      reinterpret_cast<DeliverGuardException>(hooks.FindSymbol("libart.so", kGuardQuickDeliverException)),
      IsRetiredMethodPointer, IsRetiredFieldPointer};
  if (!ConfigureRetiredMemberGuard(functions)) {
    *error = "Missing named ART thread functions for retired-member guards";
    return false;
  }
  contains_unsafe = contains;
  return true;
}

}  // namespace artdcevm::android13

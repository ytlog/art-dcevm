#include "platform/android13/class_loader_hooks.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include "platform/common/class_loader_cleanup.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_method_registry_bridge.h"

namespace artdcevm::android13 {
namespace {
using DeleteClassLoader = class_loader_cleanup::Cleanup;
constexpr auto kCleanupDeclaration = class_loader_cleanup::Declaration::kDeleteAndDestroy;
DeleteClassLoader original_delete = nullptr;
std::atomic<uint64_t> delete_calls{0};
std::atomic<uint64_t> pruned_fields{0};
std::atomic<uint64_t> pruned_methods{0};

void DeleteClassLoaderProxy(void* linker, void* thread, const void* data, bool cleanup_cha) {
  ++delete_calls;
  const bool dispatched = class_loader_cleanup::Dispatch(
      kCleanupDeclaration, original_delete, linker, thread, data, cleanup_cha,
      [](void* allocator) {
        pruned_fields += PruneRetiredFields(allocator);
        pruned_methods += PruneRetiredMethods(allocator);
      });
  if (!dispatched) std::abort();
}
}  // namespace

bool InstallClassLoaderHooks(HookEngine& hooks, std::string* error) {
  void* target = hooks.FindSymbol("libart.so", class_loader_cleanup::Symbol(kCleanupDeclaration));
  if (!target) {
    *error = "Missing named class-loader deletion symbol";
    return false;
  }
  return hooks.Install(target, reinterpret_cast<void*>(DeleteClassLoaderProxy),
                       reinterpret_cast<void**>(&original_delete), error) != nullptr;
}

uint64_t ClassLoaderDeleteCalls() { return delete_calls.load(std::memory_order_relaxed); }
uint64_t RetiredPrunedCount(int kind) {
  return kind == 0 ? pruned_fields.load(std::memory_order_relaxed)
                   : pruned_methods.load(std::memory_order_relaxed);
}

}  // namespace artdcevm::android13

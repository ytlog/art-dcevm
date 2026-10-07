#include "platform/android13/class_loader_hooks.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using DeleteClassLoader = void (*)(void*, void*, const void*, bool);
DeleteClassLoader original_delete = nullptr;
std::atomic<uint64_t> delete_calls{0};
std::atomic<uint64_t> pruned_fields{0};
std::atomic<uint64_t> pruned_methods{0};

// Fixed Android 13 class_linker.h declaration of ClassLinker::ClassLoaderData.
// This is a source declaration, not an offset recovered from a device binary.
struct ClassLoaderData13 {
  void* weak_root;
  void* class_table;
  void* allocator;
};
static_assert(sizeof(ClassLoaderData13) == 3 * sizeof(void*));

void DeleteClassLoaderProxy(void* linker, void* thread, const void* data, bool cleanup_cha) {
  // The allocator is still live before the device deletion call. Copy the
  // fixed source-declared record to avoid aliasing its private C++ type.
  ClassLoaderData13 loader_data{};
  std::memcpy(&loader_data, data, sizeof(loader_data));
  ++delete_calls;
  if (loader_data.allocator != nullptr) {
    pruned_fields += PruneRetiredFields(loader_data.allocator);
    pruned_methods += PruneRetiredMethods(loader_data.allocator);
  }
  original_delete(linker, thread, data, cleanup_cha);
}
}  // namespace

bool InstallClassLoaderHooks(HookEngine& hooks, std::string* error) {
  void* target = hooks.FindSymbol("libart.so", cas::kDeleteClassLoader);
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

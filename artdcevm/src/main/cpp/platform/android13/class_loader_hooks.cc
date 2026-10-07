#include "platform/android13/class_loader_hooks.h"

#include <dlfcn.h>
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using DeleteClassLoader = void (*)(void*, void*, const void*, bool);
DeleteClassLoader original_delete = nullptr;
DeleteClassLoader prune_retired = nullptr;

void DeleteClassLoaderProxy(void* linker, void* thread, const void* data, bool cleanup_cha) {
  // ART's ClassLoaderData still owns the allocator here. The calibrated
  // implementation only prunes its registries; this library owns dispatch and
  // always lets the device perform the actual class-loader deletion.
  prune_retired(linker, thread, data, cleanup_cha);
  original_delete(linker, thread, data, cleanup_cha);
}
}  // namespace

bool InstallClassLoaderHooks(HookEngine& hooks, void* implementation,
                             std::string* error) {
  auto prune = reinterpret_cast<DeleteClassLoader>(
      dlsym(implementation, cas::kDeleteClassLoader));
  void* target = hooks.FindSymbol("libart.so", cas::kDeleteClassLoader);
  if (!prune || !target) {
    *error = "Missing named class-loader cleanup implementation or device symbol";
    return false;
  }
  prune_retired = prune;
  return hooks.Install(target, reinterpret_cast<void*>(DeleteClassLoaderProxy),
                       reinterpret_cast<void**>(&original_delete), error) != nullptr;
}

}  // namespace artdcevm::android13

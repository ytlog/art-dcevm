#include "platform/android13/hierarchy_resolution_hooks.h"

#include <cstddef>
#include <cstdint>
#include "platform/android13/hierarchy_allocation.h"
#include <vector>
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
// Forward the fixed arm64 ObjPtr return and Handle/ObjPtr arguments opaquely.
// The source implementation checks their sizes when compiled.
using Verify = uint32_t (*)(void*, void*, const void*, void*, void*, void*,
                            const void*, void*, uint32_t, uint32_t, void*);
using Resolve = void* (*)(void*, const char*, void*);
using LinkerLookup = void* (*)(void*, void*, const char*, void*);
Verify original_verify = nullptr;
Resolve original_resolve = nullptr;
LinkerLookup original_find = nullptr;
LinkerLookup original_lookup = nullptr;

uint32_t VerifyProxy(void* thread, void* deps, const void* dex, void* klass,
                     void* cache, void* loader, const void* def, void* callbacks,
                     uint32_t log_level, uint32_t api, void* error) {
  if (!ValidationHooks::InSourceTransaction())
    return original_verify(thread, deps, dex, klass, cache, loader, def,
                           callbacks, log_level, api, error);
  HierarchyVerificationView view(klass, cache);
  const uint32_t result = original_verify(thread, deps, dex, klass, cache,
                                          loader, def, callbacks, log_level, api, error);
  return result;
}

void* ResolveProxy(void* cache, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = HierarchyVerificationView::Resolve(descriptor, loader, false);
    if (replacement) return replacement;
  }
  return original_resolve(cache, descriptor, loader);
}

void* FindProxy(void* linker, void* thread, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = HierarchyVerificationView::Resolve(descriptor, HierarchyRootPool::Get(loader), true);
    if (replacement) return replacement;
  }
  return original_find(linker, thread, descriptor, loader);
}

void* LookupProxy(void* linker, void* thread, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = HierarchyVerificationView::Resolve(descriptor, loader, true);
    if (replacement) return replacement;
  }
  return original_lookup(linker, thread, descriptor, loader);
}
}  // namespace

bool InstallHierarchyResolutionHooks(HookEngine& hooks, const DeviceProfile& profile,
                                     std::string* error) {
  if(!profile.Matched()){*error="Hierarchy resolution requires a matched device profile";return false;}
  struct Entry { const char* name; void* proxy; void** original; };
  const Entry entries[]{
      {cas::kVerifyClass, reinterpret_cast<void*>(VerifyProxy),
       reinterpret_cast<void**>(&original_verify)},
      {cas::kVerifierResolve, reinterpret_cast<void*>(ResolveProxy),
       reinterpret_cast<void**>(&original_resolve)},
      {cas::kFindClass, reinterpret_cast<void*>(FindProxy),
       reinterpret_cast<void**>(&original_find)},
      {cas::kLookupClass, reinterpret_cast<void*>(LookupProxy),
       reinterpret_cast<void**>(&original_lookup)},
  };
  std::vector<void*> tokens;
  for (const auto& entry : entries) {
    void* target = hooks.FindSymbol("libart.so", entry.name);
    if (!target) {
      *error = std::string("Missing named hierarchy resolution symbol: ") + entry.name;
    } else {
      void* token = hooks.Install(target, entry.proxy, entry.original, error);
      if (token) {
        tokens.push_back(token);
        continue;
      }
    }
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
      std::string rollback;
      if (!hooks.Remove(*it, &rollback)) *error += "; unhook failed: " + rollback;
    }
    return false;
  }
  return true;
}

}  // namespace artdcevm::android13

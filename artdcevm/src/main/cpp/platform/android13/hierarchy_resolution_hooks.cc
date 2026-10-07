#include "platform/android13/hierarchy_resolution_hooks.h"

#include <dlfcn.h>
#include <vector>
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
// Forward the fixed arm64 ObjPtr return and Handle/ObjPtr arguments opaquely.
// The source implementation checks their sizes when compiled.
using Resolve = void* (*)(void*, const char*, void*);
using LinkerLookup = void* (*)(void*, void*, const char*, void*);
Resolve original_resolve = nullptr;
Resolve replacement_resolve = nullptr;
LinkerLookup original_find = nullptr;
LinkerLookup replacement_find = nullptr;
LinkerLookup original_lookup = nullptr;
LinkerLookup replacement_lookup = nullptr;

void* ResolveProxy(void* cache, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = replacement_resolve(cache, descriptor, loader);
    if (replacement) return replacement;
  }
  return original_resolve(cache, descriptor, loader);
}

void* FindProxy(void* linker, void* thread, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = replacement_find(linker, thread, descriptor, loader);
    if (replacement) return replacement;
  }
  return original_find(linker, thread, descriptor, loader);
}

void* LookupProxy(void* linker, void* thread, const char* descriptor, void* loader) {
  if (ValidationHooks::InSourceTransaction()) {
    void* replacement = replacement_lookup(linker, thread, descriptor, loader);
    if (replacement) return replacement;
  }
  return original_lookup(linker, thread, descriptor, loader);
}
}  // namespace

bool InstallHierarchyResolutionHooks(HookEngine& hooks, void* implementation,
                                     std::string* error) {
  auto resolve = reinterpret_cast<Resolve>(
      dlsym(implementation, "ArtDcevmSourceHierarchyResolveReplacement"));
  auto find = reinterpret_cast<LinkerLookup>(
      dlsym(implementation, "ArtDcevmSourceHierarchyFindReplacement"));
  auto lookup = reinterpret_cast<LinkerLookup>(
      dlsym(implementation, "ArtDcevmSourceHierarchyLookupReplacement"));
  if (!resolve || !find || !lookup) {
    *error = "Hierarchy resolution callbacks are incomplete";
    return false;
  }
  replacement_resolve = resolve;
  replacement_find = find;
  replacement_lookup = lookup;
  struct Entry { const char* name; void* proxy; void** original; };
  const Entry entries[]{
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

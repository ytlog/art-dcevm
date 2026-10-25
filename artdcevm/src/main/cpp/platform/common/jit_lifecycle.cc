#include "platform/common/jit_lifecycle.h"
#include <android/api-level.h>
#include "platform/abi/arm64/android16/jit_code_cache.h"
#include "platform/symbols/jit_lifecycle_symbols.h"

namespace artdcevm::jit_lifecycle {
namespace {
using Cache = abi::arm64::android16::JitCodeCachePrefix;
bool (*original_remove)(void*, void*, bool) = nullptr;
void (*original_free)(void*, const void*) = nullptr;
struct ReleaseTarget { void* cache; void* method; };
thread_local ReleaseTarget releasing{};

bool RemoveMethod(void* cache, void* method, bool release_memory) {
  // ART acquires jit_mutator_lock_ inside this call; its caller already holds
  // jit_lock_. Only FreeCodeAndData reached within this scope may clean records.
  const ReleaseTarget previous = releasing;
  releasing = release_memory ? ReleaseTarget{cache, method} : ReleaseTarget{};
  const bool result = original_remove(cache, method, release_memory);
  releasing = previous;
  return result;
}
void FreeCode(void* cache, const void* code) {
  if (releasing.cache == cache) {
    auto* state = static_cast<Cache*>(cache);
    // Match the upstream non-native removal branch. Both locks are held here.
    // Do not touch JNI bookkeeping or GC's independent FreeAllMethodHeaders path.
    const auto entry = state->method_code.find(code);
    // FreeCodeAndData is a no-op for shared zygote code. Match that boundary
    // before mutating records, and require the exact method being removed.
    const auto& shared = state->shared_region.exec_pages;
    const uintptr_t address = reinterpret_cast<uintptr_t>(code);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(shared.begin);
    const bool shared_code = address >= begin && address - begin < shared.size;
    if (!shared_code && entry != state->method_code.end() &&
        entry->second == releasing.method) {
      state->zombie_code.erase(code);
      state->processed_zombie_code.erase(code);
    }
  }
  original_free(cache, code);
}
}  // namespace
bool Prepare(std::vector<Hook>* hooks, std::string* failure) {
  if (android_get_device_api_level() != 36) return true;
  using namespace symbols::jit_lifecycle;
  void* remove = symbols::Find(kRemoveMethodLocked);
  void* free = symbols::Find(kFreeCodeAndData);
  if (!remove || !free) {
    *failure = "Android 16 JIT lifecycle repair requires named removal/free services";
    return false;
  }
  hooks->push_back({remove, HookAddress(RemoveMethod), HookSlot(&original_remove), kRemoveMethodLocked.name});
  hooks->push_back({free, HookAddress(FreeCode), HookSlot(&original_free), kFreeCodeAndData.name});
  return true;
}
}  // namespace artdcevm::jit_lifecycle

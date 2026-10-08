#pragma once
#include "platform/abi/arm64/android11/class_loader.h"
#include <cstring>

namespace artdcevm::class_loader_cleanup {
// A provider selects a declaration after source/profile calibration, not from
// SDK alone: observed API34 Mainline uses PrepareToDeleteClassLoader although
// android-14.0.0_r1 still uses DeleteClassLoader (14 r50 is also split).
// This is not a symbol fallback.
enum class Declaration {
  kDeleteAndDestroy,       // Android 11–14 r1: original destroys this record.
  kPrepareForBatchDelete  // Android 14 r50 / 15–17 r1: destroy after ALL prepares.
};
using Cleanup = void (*)(void*, void*, const void*, bool);
inline const char* Symbol(Declaration declaration) {
  switch (declaration) {
    case Declaration::kDeleteAndDestroy:
      return "_ZN3art11ClassLinker17DeleteClassLoaderEPNS_6ThreadERKNS0_15ClassLoaderDataEb";
    case Declaration::kPrepareForBatchDelete:
      return "_ZN3art11ClassLinker26PrepareToDeleteClassLoaderEPNS_6ThreadERKNS0_15ClassLoaderDataEb";
    default: return nullptr;
  }
}

// The record and allocator must be live and match the selected ARM64 source
// declaration. The caller retains the original ART mutator/loader-lock context.
// Prune borrowed member registries before native metadata can be reclaimed.
// We never delete either pointer: batch preparation must keep every allocator
// live until all cross-loader dependency analysis completes in ART's caller.
// Prune must not retain the record, suspend, or change the loader destruction
// schedule. A valid call forwards exactly once with its original arguments.
template<class Prune>
bool Dispatch(Declaration declaration, Cleanup original, void* linker,
              void* thread, const void* data, bool cleanup_cha, Prune prune) {
  if (!Symbol(declaration) || !original || !data) return false;
  class_loader_abi::arm64::ClassLoaderData snapshot{};
  std::memcpy(&snapshot, data, sizeof(snapshot));
  if (snapshot.allocator) prune(snapshot.allocator);
  original(linker, thread, data, cleanup_cha);
  return true;
}
}  // namespace artdcevm::class_loader_cleanup

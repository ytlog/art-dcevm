#pragma once
#include <vector>
#include <cstdint>
#include "platform/common/art_hook.h"

// Keep ART's invocation checks when verified code re-resolves an evolved method.
namespace artdcevm::invoke_kind {
bool Prepare(std::vector<Hook>* hooks);
void Installed(bool installed);
void ObserveClassLinker(void* linker);
// Monotonic: checked resolution also preserves valid calls after an aborted batch.
bool Enable();
bool Protecting();
// Commit under ART suspend-all/JIT suspension; IsCompilable gates both JIT and inlining.
void KeepInterpreted(void* method, uint32_t precompiled_flag);
}  // namespace artdcevm::invoke_kind

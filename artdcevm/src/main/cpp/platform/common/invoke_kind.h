#pragma once
#include <vector>
#include <cstdint>
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/runtime_thread.h"
#include "platform/abi/arm64/common/interface_runtime.h"

// Keep ART's invocation checks when verified code re-resolves an evolved method.
namespace artdcevm::invoke_kind {
bool Prepare(std::vector<Hook>* hooks);
void Installed(bool installed);
void ObserveClassLinker(void* linker);
// Monotonic: checked resolution also preserves valid calls after an aborted batch.
bool Enable();
bool Protecting();
// Caller supplies Native-probed Thread declarations and ART's existing admission root.
void ObserveScope(void* thread, const abi::arm64::RuntimeThread* thread_layout,
                  const abi::arm64::InterfaceRuntime* layout, abi::arm64::Handle target);
void ObserveClassLayout(size_t access_flags, size_t dex_cache, size_t ext_data, size_t obsolete_methods);
bool SelfCalls();
// Commit under ART suspend-all/JIT suspension; IsCompilable gates both JIT and inlining.
void KeepInterpreted(void* method, uint32_t precompiled_flag);
}  // namespace artdcevm::invoke_kind

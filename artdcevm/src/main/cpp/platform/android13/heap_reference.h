#pragma once

#include "api/hook_engine.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace artdcevm::android13 {
#ifdef ARTDCEVM_TESTING
uint64_t HeapReferenceMarkCalls();
#endif
bool ConfigureHeapReferenceReader(HookEngine& hooks, std::string* error);
// Equivalent to release Baker GetFieldObject<...,kWithReadBarrier>. The caller
// holds the mutator lock and provides a rooted, to-space owner and aligned
// HeapReference32 field. This helper does not root or retain the owner/target.
// Ordinary ART getters use kAlwaysUpdateField=false and never CAS the field;
// a marker result must not overwrite a value concurrently stored by a mutator.
void* ReadHeapReference(void* object, size_t offset, bool is_volatile = false);
}

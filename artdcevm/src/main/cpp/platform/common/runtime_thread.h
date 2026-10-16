#pragma once
#include <jni.h>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_thread.h"

// Shared by ordinary-class and interface transactions. No interface dependency.
namespace artdcevm::runtime_thread {
// Resolve named ART services once; no hook or collector is implemented here.
void Prepare();
bool Available();
// Native phase of every routed transaction, before ART's Runnable hooks.
// Results belong to transaction::Context, including across nested transactions.
void Probe(JNIEnv* jni);
bool Ready() noexcept;
const char* Rejection() noexcept;
// Only a Native-probed declaration can be used by interface HandleScope probes.
bool Supports(int release) noexcept;
void* Current() noexcept;
const abi::arm64::RuntimeThread& Layout() noexcept;
// Runnable phase only; callers must check Ready before probing metadata.
// A nonnull read without a verified thread is an invariant failure, never raw.
void* Read(uint32_t reference) noexcept;
}  // namespace artdcevm::runtime_thread

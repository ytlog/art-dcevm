#pragma once
#include <jni.h>
#include <cstdint>
#include <optional>
#include "platform/abi/arm64/common/runtime_thread.h"

namespace artdcevm::transaction { struct Part; }
namespace artdcevm::runtime_thread {
void Prepare();
bool Available();
void Probe(JNIEnv* jni);
const char* Rejection() noexcept;

// Only Ready can construct this transaction-scoped proof. Passing an outer or
// expired proof is still a fatal invariant failure; it never permits raw reads.
class VerifiedThread {
 public:
  VerifiedThread(const VerifiedThread&) = default;
  VerifiedThread& operator=(const VerifiedThread&) = default;
 private:
  explicit VerifiedThread(const transaction::Part* state) : state_(state) {}
  const transaction::Part* state_;
  friend std::optional<VerifiedThread> Ready() noexcept;
  friend void* Current(const VerifiedThread&) noexcept;
};
std::optional<VerifiedThread> Ready() noexcept;
void* Current(const VerifiedThread& thread) noexcept;
const abi::arm64::RuntimeThread& Layout(const VerifiedThread& thread) noexcept;
// Only candidates accepted by this thread's Native probe can be selected.
const abi::arm64::RuntimeThread* LayoutFor(const VerifiedThread& thread, int release) noexcept;
// Runnable phase only. Every caller must carry its Native probe's proof.
void* Read(const VerifiedThread& thread, uint32_t reference) noexcept;
}  // namespace artdcevm::runtime_thread

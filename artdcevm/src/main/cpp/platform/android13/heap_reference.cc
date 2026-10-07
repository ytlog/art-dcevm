#include "platform/android13/heap_reference.h"
#include "platform/android13/heap_reference_abi.h"
#include <cstdint>
#ifdef ARTDCEVM_TESTING
#include <atomic>
#endif

namespace artdcevm::android13 {
namespace {
using Mark = void* (*)(void*);
Mark mark = nullptr;
#ifdef ARTDCEVM_TESTING
std::atomic<uint64_t> mark_calls{0};
#endif
}

#ifdef ARTDCEVM_TESTING
uint64_t HeapReferenceMarkCalls() { return mark_calls.load(std::memory_order_relaxed); }
#endif

bool ConfigureHeapReferenceReader(HookEngine& hooks, std::string* error) {
  auto target = reinterpret_cast<Mark>(
      hooks.FindSymbol("libart.so", heap_reference_abi::kReadBarrierMark));
  if (!target) {
    *error = "Missing named ART Baker read-barrier marker";
    return false;
  }
  mark = target;
  return true;
}

void* ReadHeapReference(void* object, size_t offset, bool is_volatile) {
  using namespace heap_reference_abi;
  auto* bytes = static_cast<unsigned char*>(object);
  // ReadBarrier::IsGray(Object*) explicitly provides this acquire alternative
  // to the arm64 fake address dependency. It orders monitor before reference.
  const uint32_t monitor = __atomic_load_n(
      reinterpret_cast<const uint32_t*>(bytes + kMonitorOffset), __ATOMIC_ACQUIRE);
  const bool gray = ((monitor >> kReadBarrierShift) & kReadBarrierMask) == kGrayState;
  const uint32_t reference = __atomic_load_n(
      reinterpret_cast<const uint32_t*>(bytes + offset),
      is_volatile ? __ATOMIC_SEQ_CST : __ATOMIC_RELAXED);
  auto* target = reinterpret_cast<void*>(static_cast<uintptr_t>(reference));
  // The locked source also invokes Mark(nullptr) for gray owners. The device
  // collector owns this null path and all relocation/forwarding decisions.
  if (!gray) return target;
#ifdef ARTDCEVM_TESTING
  mark_calls.fetch_add(1, std::memory_order_relaxed);
#endif
  return mark(target);
}
}

#pragma once
// Source: https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/thread.h?format=TEXT
// Fixed AOSP declaration prefix only. No SDK selection and no owned-device calibration.
// Derived from runtime/thread.h, managed_stack.h and runtime_stats.h at the commit below.
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android16 {
enum class WeakRefAccessState : int32_t { kVisiblyEnabled = 0, kEnabled, kDisabled };
template<class T> struct __attribute__((packed, aligned(sizeof(T)))) Atomic : std::atomic<T> {};
struct __attribute__((packed, aligned(4))) RuntimeStats {
  uint64_t allocated_objects, allocated_bytes, freed_objects, freed_bytes;
  uint64_t gc_for_alloc_count, class_init_count, class_init_time_ns;
};
struct __attribute__((packed, aligned(4))) ManagedStack {
  struct TaggedTopQuickFrame { uintptr_t tagged_sp_; } tagged_top_quick_frame_;
  ManagedStack* link_;
  void* top_shadow_frame_;
};
struct alignas(4) Tls32 {
  Atomic<uint32_t> state_and_flags;
  int suspend_count;
  uint32_t thin_lock_thread_id, tid;
  const uint32_t daemon;
  uint32_t throwing_OutOfMemoryError, no_thread_suspension, thread_exit_check_count;
  uint32_t is_gc_marking, is_deopt_check_required;
  Atomic<uint32_t> interrupted;
  Atomic<int32_t> park_state_;
  mutable std::atomic<WeakRefAccessState> weak_ref_access_enabled;
  uint32_t disable_thread_flip_count;
  int user_code_suspend_count;
  uint32_t force_interpreter_count, make_visibly_initialized_counter, define_class_counter;
  mutable std::atomic<uint32_t> num_name_readers;
  uint32_t shared_method_hotness;
};
struct alignas(8) Tls64 { uint64_t trace_clock_base; RuntimeStats stats; };
struct alignas(sizeof(void*)) TlsPointersPrefix {
  uint8_t* card_table;
  void* exception;
  uint8_t* stack_end;
  ManagedStack managed_stack;
  std::atomic<uintptr_t*> suspend_trigger;
};
// Thread has no base class or virtual members and its first instance members
// are tls32_, tls64_, tlsPtr_. The suffix is intentionally not instantiated.
struct ThreadTlsPrefix { Tls32 tls32_; Tls64 tls64_; TlsPointersPrefix tlsPtr_; };
static_assert(sizeof(void*) == 8 && sizeof(int) == 4);
static_assert(sizeof(Atomic<uint32_t>) == 4 && alignof(Atomic<uint32_t>) == 4);
static_assert(sizeof(Atomic<int32_t>) == 4 && alignof(Atomic<int32_t>) == 4);
static_assert(sizeof(std::atomic<WeakRefAccessState>) == 4);
static_assert(sizeof(std::atomic<uintptr_t*>) == 8);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(sizeof(RuntimeStats) == 56 && alignof(RuntimeStats) == 4);
static_assert(sizeof(ManagedStack) == 24 && alignof(ManagedStack) == 4);
}  // namespace artdcevm::abi::arm64::android16

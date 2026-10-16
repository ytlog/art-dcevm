#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64 {
// Identical source prefixes: runtime_stats.h, managed_stack.h and thread.h,
// android-11.0.0_r1 through android-17.0.0_r1. Read declarations only.
struct RuntimeStatsPrefix {
  uint64_t allocated_objects, allocated_bytes, freed_objects, freed_bytes;
  uint64_t gc_for_alloc_count, class_init_count, class_init_time_ns;
};
struct ThreadTls64Prefix { uint64_t trace_clock_base; RuntimeStatsPrefix stats; };
struct ManagedStackPrefix { uintptr_t tagged_top_quick_frame; void* link; void* top_shadow_frame; };
struct ThreadTlsPtrPrefix {
  uint8_t* card_table;
  void* exception;
  void* stack_end;
  ManagedStackPrefix managed_stack;
  void* suspend_trigger;  // Atomic pointer since 15; same prefix storage, never accessed.
  void* jni_env;
  void* tmp_jni_env;
  void* self;
  void* opeer;
  void* jpeer;
  void* stack_begin;
  size_t stack_size;
  void* deps_or_stack_trace_sample;
  void* wait_next;
  void* monitor_enter_object;
  void* top_handle_scope;
};
static_assert(sizeof(ThreadTls64Prefix) == 64 && sizeof(ManagedStackPrefix) == 24);
static_assert(offsetof(ThreadTlsPtrPrefix, jni_env) == 56);
static_assert(offsetof(ThreadTlsPtrPrefix, top_handle_scope) == 136);
}  // namespace artdcevm::abi::arm64

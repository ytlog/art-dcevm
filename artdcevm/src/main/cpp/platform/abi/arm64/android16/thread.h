#pragma once
#include <cstddef>
#include <cstdint>

// Release arm64 prefixes from android-16.0.0_r1 runtime/thread.h,
// runtime_stats.h, managed_stack.h and handle_scope.h. Current thread only.
namespace artdcevm::abi::arm64::android16 {
struct ThreadTls32 {
  uint32_t state_and_flags;
  int32_t suspend_count;
  uint32_t thin_lock_thread_id, tid, daemon, throwing_out_of_memory;
  uint32_t no_thread_suspension, thread_exit_check_count;
  uint32_t is_gc_marking, is_deopt_check_required, interrupted;
  int32_t park_state;
  uint32_t weak_ref_access_enabled, disable_thread_flip_count;
  int32_t user_code_suspend_count;
  uint32_t force_interpreter_count, make_visibly_initialized_counter;
  uint32_t define_class_counter, num_name_readers, shared_method_hotness;
};
struct RuntimeStats { uint64_t allocated_objects, allocated_bytes, freed_objects, freed_bytes;
  uint64_t gc_for_alloc_count, class_init_count, class_init_time_ns; };
struct ThreadTls64 { uint64_t trace_clock_base; RuntimeStats stats; };
struct ManagedStack { uintptr_t tagged_top_quick_frame; void* link; void* top_shadow_frame; };
struct ThreadTlsPtr {
  uint8_t* card_table;
  void* exception;
  void* stack_end;
  ManagedStack managed_stack;
  void* suspend_trigger;
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
struct Thread { ThreadTls32 tls32; ThreadTls64 tls64; ThreadTlsPtr tls_ptr; };
#pragma pack(push, 4)
// capacity_ is called number_of_references here; size_ is new since Android 15.
struct HandleScopeHeader { void* link; int32_t number_of_references; uint32_t size; };
#pragma pack(pop)
static_assert(sizeof(ThreadTls32) == 80 && sizeof(ThreadTls64) == 64);
static_assert(offsetof(Thread, tls_ptr) == 144 && offsetof(ThreadTls32, is_gc_marking) == 32);
static_assert(sizeof(HandleScopeHeader) == 16);
}  // namespace artdcevm::abi::arm64::android16

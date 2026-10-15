#pragma once
#include <cstddef>
#include <cstdint>

// Release arm64 prefix from android-13.0.0_r1 runtime/thread.h,
// runtime_stats.h and managed_stack.h. Only the initiating thread is accessed.
namespace artdcevm::abi::arm64::android13 {
struct ThreadTls32 {
  uint32_t state_and_flags;
  int32_t suspend_count;
  uint32_t thin_lock_thread_id;
  uint32_t tid;
  uint32_t daemon;
  uint32_t throwing_out_of_memory;
  uint32_t no_thread_suspension;
  uint32_t thread_exit_check_count;
  uint32_t is_transitioning_to_runnable;
  uint32_t is_gc_marking;
  uint32_t interrupted;
  int32_t park_state;
  uint32_t weak_ref_access_enabled;
  uint32_t disable_thread_flip_count;
  int32_t user_code_suspend_count;
  uint32_t force_interpreter_count;
  uint32_t make_visibly_initialized_counter;
  uint32_t define_class_counter;
  uint32_t num_name_readers;
  uint32_t shared_method_hotness;
};
struct RuntimeStats {
  uint64_t allocated_objects;
  uint64_t allocated_bytes;
  uint64_t freed_objects;
  uint64_t freed_bytes;
  uint64_t gc_for_alloc_count;
  uint64_t class_init_count;
  uint64_t class_init_time_ns;
};
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
// handle_scope.h: PACKED(4) BaseHandleScope with references immediately
// after link_ and number_of_references_. Root storage is allocated separately.
#pragma pack(push, 4)
struct HandleScopeHeader { void* link; int32_t number_of_references; };
#pragma pack(pop)
static_assert(sizeof(HandleScopeHeader) == 12);
// gc/accounting/card_table.h (API 33).
inline constexpr size_t kCardShift = 10;
inline constexpr uint8_t kCardDirty = 0x70;
static_assert(sizeof(ThreadTls32) == 80 && sizeof(ThreadTls64) == 64);
static_assert(offsetof(Thread, tls_ptr) == 144);
}  // namespace artdcevm::abi::arm64::android13

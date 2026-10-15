#pragma once
#include "platform/abi/arm64/common/interface_thread_prefix.h"

// Prepared from android-12.0.0_r1 and android-12.1.0_r1 thread.h/handle_scope.h.
// Not runtime-enabled; only the initiating thread would be accessed.
namespace artdcevm::abi::arm64::android12 {
struct ThreadTls32 {
  uint32_t state_and_flags;
  int32_t suspend_count;
  uint32_t thin_lock_thread_id;
  uint32_t tid;
  uint32_t daemon;
  uint32_t throwing_out_of_memory;
  uint32_t no_thread_suspension;
  uint32_t thread_exit_check_count;
  uint32_t handling_signal;
  uint32_t is_transitioning_to_runnable;
  uint32_t ready_for_debug_invoke;
  uint32_t debug_method_entry;
  uint32_t is_gc_marking;
  uint32_t interrupted;
  int32_t park_state;
  uint32_t weak_ref_access_enabled;
  uint32_t disable_thread_flip_count;
  int32_t user_code_suspend_count;
  uint32_t force_interpreter_count;
  uint32_t use_mterp;
  uint32_t make_visibly_initialized_counter;
  uint32_t define_class_counter;
};
using ThreadTls64 = InterfaceThreadTls64;
using ThreadTlsPtr = InterfaceThreadTlsPtr;
struct Thread { ThreadTls32 tls32; ThreadTls64 tls64; ThreadTlsPtr tls_ptr; };
#pragma pack(push, 4)
struct HandleScopeHeader { void* link; int32_t number_of_references; };
#pragma pack(pop)
static_assert(sizeof(ThreadTls32) == 88 && offsetof(Thread, tls_ptr) == 152);
static_assert(offsetof(ThreadTls32, tid) == 12);
static_assert(offsetof(ThreadTls32, is_gc_marking) == 48);
static_assert(sizeof(HandleScopeHeader) == 12);
}  // namespace artdcevm::abi::arm64::android12

#pragma once
#include "platform/abi/arm64/runtime_container.h"
namespace artdcevm::abi::arm64::android12 {
using runtime::PlatformContainer;
// Fixed release Runtime prefix through jit_code_cache_; no containers or roots
// are traversed here. Mainline/vendor compatibility needs independent calibration.
struct RuntimePrefix {
  uint64_t callee_save_methods[6];
  uint32_t preallocated_exceptions[4];
  void* resolution_method;
  void* imt_conflict_method;
  void* imt_unimplemented_method;
  uint32_t sentinel;
  uint32_t instruction_set;
  void* compiler_callbacks;
  bool is_zygote;
  bool is_primary_zygote;
  bool is_system_server;
  bool must_relocate;
  bool concurrent_gc_enabled;
  bool explicit_gc_disabled;
  bool image_dex2oat_enabled;
  PlatformContainer compiler_executable;
  PlatformContainer compiler_options;
  PlatformContainer image_compiler_options;
  PlatformContainer image_location;
  PlatformContainer boot_class_path;
  PlatformContainer boot_class_path_locations;
  PlatformContainer boot_class_path_checksums;
  PlatformContainer class_path_string;
  PlatformContainer properties;
  PlatformContainer agent_specs;
  PlatformContainer agents;
  PlatformContainer plugins;
  size_t default_stack_size;
  uint32_t finalizer_timeout_ms;
  void* heap;
  void* jit_arena_pool;
  void* arena_pool;
  void* low_4gb_arena_pool;
  void* linear_alloc;
  size_t max_spins_before_thin_lock_inflation;
  void* monitor_list;
  void* monitor_pool;
  void* thread_list;
  void* intern_table;
  void* class_linker;
  void* signal_catcher;
  void* jni_id_manager;
  void* java_vm;
  void* jit;
  void* jit_code_cache;
};
static_assert(alignof(RuntimePrefix)==8);
}

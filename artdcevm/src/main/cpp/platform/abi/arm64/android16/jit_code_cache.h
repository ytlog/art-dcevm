#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace artdcevm::abi::arm64::android16 {
// android-16.0.0_r1: runtime/jit/jit_code_cache.h, jit_memory_region.h,
// runtime/base/mutex.h and libartbase/base/mem_map.h. Android uses futexes.
// Prefix declarations only; no destructor is run on ART-owned objects.
struct JitConditionVariable {
  const char* name;
  void* guard;
  std::atomic<int32_t> sequence;
  int32_t waiters;
};
struct JitMemMap {
  std::string name;
  uint8_t* begin;
  size_t size;
  void* base_begin;
  size_t base_size;
  int protection;
  bool reuse, already_unmapped;
  size_t redzone_size;
};
struct JitMemoryRegion {
  size_t initial_capacity, max_capacity, current_capacity, data_end, exec_end;
  size_t used_memory_for_code, used_memory_for_data;
  JitMemMap data_pages, writable_data_pages, exec_pages, non_exec_pages;
  void* data_mspace;
  void* exec_mspace;
};
struct JitStubKey {
  const char* shorty;
  bool is_static, is_fast_native, is_critical_native, is_synchronized;
};
struct JitStubData { const void* code; std::vector<void*> methods; };
// SafeMap contains only its std::map member; represent that member directly.
struct JitZygoteMap {
  const void* entries;
  size_t size;
  JitMemoryRegion* region;
  const void* compilation_state;
};
struct JitCodeCachePrefix {
  std::atomic<bool> weak_access_enabled;
  JitConditionVariable inline_cache_condition;
  size_t reserved_capacity;
  JitMemoryRegion shared_region, private_region;
  std::map<JitStubKey, JitStubData> jni_stubs;
  std::map<const void*, void*> method_code;
  std::map<void*, std::vector<const void*>> reversed_method_code;
  std::map<void*, const void*> saved_compiled_methods, osr_code;
  std::set<const void*> zombie_code;
  std::set<void*> zombie_jni_code;
  std::map<void*, void*> profiling_infos;
  JitZygoteMap zygote;
  JitConditionVariable collection_condition;
  bool collection_in_progress;
  std::atomic<bool> gc_task_scheduled;
  void* live_bitmap;
  bool garbage_collect_code;
  std::set<const void*> processed_zombie_code;
};
static_assert(sizeof(JitConditionVariable) == 24);
static_assert(sizeof(JitMemMap) == 72);
static_assert(sizeof(JitMemoryRegion) == 360);
static_assert(sizeof(std::map<const void*, void*>) == 24);
static_assert(sizeof(std::set<const void*>) == 24);
}  // namespace artdcevm::abi::arm64::android16

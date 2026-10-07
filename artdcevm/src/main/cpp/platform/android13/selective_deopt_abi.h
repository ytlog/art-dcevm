#pragma once
#include "platform/android13/runtime_access_abi.h"
namespace artdcevm::android13::selective_abi {
#pragma pack(push,4)
struct RuntimeStats { uint64_t counters[7]; };
#pragma pack(pop)
struct RuntimePrefix {
  runtime_access_abi::RuntimePrefix services;
  void* jit_options; void* thread_pool; size_t thread_pool_ref_count;
  void* fault_message; size_t threads_being_born; void* shutdown_cond;
  bool shutting_down, shutting_down_started, started, finished_starting;
  void* vfprintf_hook; void* exit_hook; void* abort_hook;
  bool stats_enabled; RuntimeStats stats;
  bool running_on_memory_tool; void* trace_config;
  alignas(8) unsigned char instrumentation;
};
// Platform __tree's nodes (tree, map): no NDK container accesses this storage.
struct TreeNode { TreeNode* left; TreeNode* right; TreeNode* parent; bool black; };
struct Frame { void* receiver; void* method; uintptr_t return_pc; bool interpreter_entry; uint64_t force_deopt_id; };
struct MapNode { TreeNode base; uintptr_t key; Frame value; };
struct Tree { void* begin; TreeNode* root; size_t size; };
inline constexpr size_t kThreadInstrumentationStack=304;
inline constexpr uint8_t kDebugFlag=2;
inline Frame* FindFrame(void* raw,uintptr_t key) {
  auto* tree=static_cast<Tree*>(raw);
  for (auto* node=tree->root;node;) {
    auto* value=reinterpret_cast<MapNode*>(node);
    if (key<value->key) node=node->left;
    else if (key>value->key) node=node->right;
    else return &value->value;
  }
  return nullptr;
}
}

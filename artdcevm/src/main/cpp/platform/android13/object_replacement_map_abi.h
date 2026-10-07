#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::object_map_abi {
// Locked platform libc++ __hash_table with empty allocator/hash/equal and 64-bit ObjPtr.
struct NodeBase {NodeBase* next;};
struct Node:NodeBase {uint64_t hash;void* key;void* value;};
struct Map {NodeBase** buckets;uint64_t bucket_count;NodeBase sentinel;uint64_t size;float max_load_factor;uint32_t padding;};
static_assert(sizeof(Node)==32 && sizeof(Map)==40);
static_assert(offsetof(Map,sentinel)==16 && offsetof(Map,size)==24 && offsetof(Map,max_load_factor)==32);
}

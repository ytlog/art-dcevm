#include "platform/common/object_replacement_map.h"
#include <algorithm>
#include <unordered_set>
#include <stdexcept>
#include <limits>
namespace artdcevm::object_replacement {
uint64_t HashReplacementPointer(const void* pointer) {
  // platform utility: hash<T*> -> cityhash64 on exactly eight little-endian bytes.
  const uint64_t bits=reinterpret_cast<uintptr_t>(pointer);
  const uint64_t u=uint64_t{8}+uint32_t(uint32_t(bits)<<3),v=uint32_t(bits>>32);
  constexpr uint64_t mul=0x9ddfea08eb382d69ULL;
  uint64_t a=(u^v)*mul;a^=a>>47;uint64_t b=(v^a)*mul;b^=b>>47;return b*mul;
}
ObjectReplacementMap::ObjectReplacementMap(const std::vector<std::pair<void*,void*>>& entries,size_t count) {
  static_assert(sizeof(void*)==8);
  std::unordered_set<void*> seen;
  nodes_.reserve(entries.size());
  for(const auto& entry:entries)if(seen.insert(entry.first).second)
    nodes_.push_back({{nullptr},HashReplacementPointer(entry.first),entry.first,entry.second});
  if(nodes_.empty()){map_.max_load_factor=1.0f;return;}
  if(!count){count=2;while(count<nodes_.size()){if(count>std::numeric_limits<size_t>::max()/2)throw std::length_error("Object replacement map capacity");count*=2;}}
  // Power-of-two counts let our builder use the exact __constrain_hash reduction.
  if(count<2 || (count&(count-1)))throw std::invalid_argument("Object replacement bucket count must be power of two >=2");
  buckets_.assign(count,nullptr);
  std::stable_sort(nodes_.begin(),nodes_.end(),[count](const auto& a,const auto& b){return (a.hash&(count-1))<(b.hash&(count-1));});
  map_.buckets=buckets_.data();map_.bucket_count=count;map_.size=nodes_.size();map_.max_load_factor=1.0f;
  object_map_abi::NodeBase* previous=&map_.sentinel;
  for(auto& node:nodes_){size_t bucket=node.hash&(count-1);if(!buckets_[bucket])buckets_[bucket]=previous;previous->next=&node;previous=&node;}
}
bool VerifyObjectReplacementMap(const ObjectReplacementMap& map,std::string* error) {
  const auto& view=*static_cast<const object_map_abi::Map*>(map.BorrowedMap());
  auto fail=[&](const char* message){if(error)*error=message;return false;};
  if(view.size!=map.Size() || view.bucket_count!=map.BucketCount() || view.max_load_factor!=1.0f)
    return fail("Object replacement map header is inconsistent");
  if(!map.Size()) {
    if(view.buckets || view.bucket_count || view.sentinel.next)
      return fail("Empty object replacement map contains storage");
    return true;
  }
  if(view.buckets!=map.buckets_.data() || !view.buckets || view.bucket_count<2 || (view.bucket_count&(view.bucket_count-1)))
    return fail("Object replacement map bucket storage is invalid");
  std::vector<const object_map_abi::NodeBase*> predecessors(view.bucket_count,nullptr);
  std::unordered_set<void*> keys;
  const object_map_abi::NodeBase* previous=&view.sentinel;
  const object_map_abi::NodeBase* next=view.sentinel.next;
  size_t last_bucket=0;
  for(size_t i=0;i<map.Size();++i) {
    // Validate membership before dereferencing a chain pointer. The owned node
    // vector is in bucket order and must be visited exactly once in full.
    const auto* node=map.Nodes()+i;
    if(next!=node) return fail("Object replacement map chain is inconsistent");
    if(node->hash!=HashReplacementPointer(node->key) || !keys.insert(node->key).second)
      return fail("Object replacement map contains invalid hashes or duplicate keys");
    const size_t bucket=node->hash&(view.bucket_count-1);
    if(i && bucket<last_bucket) return fail("Object replacement map bucket order is inconsistent");
    if(!predecessors[bucket]) predecessors[bucket]=previous;
    previous=node;next=node->next;last_bucket=bucket;
  }
  if(next) return fail("Object replacement map chain exceeds owned storage");
  for(size_t bucket=0;bucket<view.bucket_count;++bucket)
    if(view.buckets[bucket]!=predecessors[bucket])
      return fail("Object replacement map bucket predecessor is inconsistent");
  return true;
}
}

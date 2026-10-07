#include "platform/android13/object_replacement_map.h"
#include <algorithm>
#include <unordered_set>
#include <stdexcept>
#include <limits>
#include <dlfcn.h>
namespace artdcevm::android13 {
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
bool VerifyObjectReplacementMap(void* implementation,const ObjectReplacementMap& map,std::string* error) {
  using Probe=bool (*)(const void*,const object_map_abi::Node*,size_t);
  auto probe=reinterpret_cast<Probe>(dlsym(implementation,"ArtDcevmSourceProbeObjectReplacementMap"));
  if(!probe||!probe(map.BorrowedMap(),map.Nodes(),map.Size())){*error="Incompatible platform object replacement map ABI";return false;}return true;
}
bool ConfigureObjectReplacementMaps(void* implementation,std::string* error) {
  ObjectReplacementMap empty({});if(!VerifyObjectReplacementMap(implementation,empty,error))return false;
  std::vector<std::pair<void*,void*>> entries;
  // Aligned inert pointer values exercise nonzero high bits, shift truncation and collisions.
  for(uintptr_t i=1;i<=96;++i)entries.emplace_back(reinterpret_cast<void*>(0x100000000ULL+i*0x10000008ULL),reinterpret_cast<void*>(0x20000000ULL+i*8));
  entries.emplace_back(entries.front().first,reinterpret_cast<void*>(0x30000000));
  ObjectReplacementMap many(entries,128),collisions(entries,2);
  return VerifyObjectReplacementMap(implementation,many,error)&&VerifyObjectReplacementMap(implementation,collisions,error);
}
}

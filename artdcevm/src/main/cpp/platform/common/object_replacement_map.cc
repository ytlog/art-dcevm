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
  nodes_.reserve(entries.size());entries_.reserve(entries.size());
  for(const auto& entry:entries)if(seen.insert(entry.first).second){
    entries_.push_back(entry);nodes_.push_back({{nullptr},HashReplacementPointer(entry.first),entry.first,entry.second});
  }
  verification_keys_.resize(nodes_.size());
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
bool ObjectReplacementMap::CollectManagedRoots(const prepared_roots::Registrar& registrar){
  for(auto& entry:entries_)if(!roots_.Add(&entry.first,registrar)||!roots_.Add(&entry.second,registrar))return false;
  roots_.Seal();return true;
}
void ObjectReplacementMap::RefreshManagedRoots(const prepared_roots::Resolver& resolver)noexcept{
  roots_.Refresh(resolver);Rebuild();
}
void ObjectReplacementMap::Rebuild()noexcept{
  map_.sentinel.next=nullptr;
  std::fill(buckets_.begin(),buckets_.end(),nullptr);
  for(size_t i=0;i<entries_.size();++i){const auto& entry=entries_[i];
    nodes_[i]={{nullptr},HashReplacementPointer(entry.first),entry.first,entry.second};}
  if(nodes_.empty())return;
  // In-place heapsort bounds finalization to O(N log N), with no scratch
  // allocation. Root bindings use unsorted entries_, never reordered nodes.
  const size_t mask=buckets_.size()-1;
  auto sift=[&](size_t root,size_t limit){
    while(root<limit/2){size_t child=root*2+1;
      if(child+1<limit&&(nodes_[child].hash&mask)<(nodes_[child+1].hash&mask))++child;
      if((nodes_[root].hash&mask)>=(nodes_[child].hash&mask))break;
      std::swap(nodes_[root],nodes_[child]);root=child;
    }
  };
  for(size_t start=nodes_.size()/2;start;--start)sift(start-1,nodes_.size());
  for(size_t end=nodes_.size();end>1;--end){std::swap(nodes_[0],nodes_[end-1]);sift(0,end-1);}
  object_map_abi::NodeBase* previous=&map_.sentinel;
  for(auto& node:nodes_){size_t bucket=node.hash&mask;if(!buckets_[bucket])buckets_[bucket]=previous;previous->next=&node;previous=&node;}
}
bool ObjectReplacementMap::Maps(void*source,void*destination)const noexcept{
 return destination&&Replacement(source)==destination;
}
void* ObjectReplacementMap::Replacement(void*source)const noexcept{
 if(!source||buckets_.empty())return nullptr;
 const size_t mask=buckets_.size()-1,bucket=HashReplacementPointer(source)&mask;
 auto current=std::lower_bound(nodes_.begin(),nodes_.end(),bucket,[mask](const auto&node,size_t value){return (node.hash&mask)<value;});
 for(;current!=nodes_.end()&&(current->hash&mask)==bucket;++current)
  if(current->key==source)return current->value;
 return nullptr;
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
  if(map.verification_keys_.size()!=map.Size())return fail("Object replacement verification storage changed");
  const object_map_abi::NodeBase* previous=&view.sentinel;
  const object_map_abi::NodeBase* next=view.sentinel.next;
  size_t last_bucket=0,checked_bucket=0;
  for(size_t i=0;i<map.Size();++i) {
    const auto* node=map.Nodes()+i;
    // Never dereference an unowned chain address.
    if(next!=node)return fail("Object replacement map chain is inconsistent");
    if(node->hash!=HashReplacementPointer(node->key))return fail("Object replacement map contains an invalid hash");
    map.verification_keys_[i]=reinterpret_cast<uintptr_t>(node->key);
    const size_t bucket=node->hash&(view.bucket_count-1);
    if(i&&bucket<last_bucket)return fail("Object replacement map bucket order is inconsistent");
    if(!i||bucket!=last_bucket){
      while(checked_bucket<bucket)if(view.buckets[checked_bucket++])
        return fail("Object replacement map empty bucket has a predecessor");
      if(view.buckets[bucket]!=previous)return fail("Object replacement map bucket predecessor is inconsistent");
      checked_bucket=bucket+1;
    }
    previous=node;next=node->next;last_bucket=bucket;
  }
  if(next)return fail("Object replacement map chain exceeds owned storage");
  while(checked_bucket<view.bucket_count)if(view.buckets[checked_bucket++])
    return fail("Object replacement map empty bucket has a predecessor");
  std::sort(map.verification_keys_.begin(),map.verification_keys_.end());
  for(size_t i=1;i<map.verification_keys_.size();++i)
    if(map.verification_keys_[i]==map.verification_keys_[i-1])return fail("Object replacement map contains duplicate keys");
  return true;
}
}

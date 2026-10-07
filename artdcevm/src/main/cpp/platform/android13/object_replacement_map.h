#pragma once
#include "platform/android13/object_replacement_map_abi.h"
#include <utility>
#include <vector>
#include <string>
namespace artdcevm::android13 {
uint64_t HashReplacementPointer(const void*);
class ObjectReplacementMap {
 public:
  // The first duplicate key wins, matching platform unordered_map::emplace.
  explicit ObjectReplacementMap(const std::vector<std::pair<void*,void*>>& entries,size_t buckets=0);
  ObjectReplacementMap(const ObjectReplacementMap&)=delete;
  ObjectReplacementMap& operator=(const ObjectReplacementMap&)=delete;
  ObjectReplacementMap(ObjectReplacementMap&&)=delete;
  ObjectReplacementMap& operator=(ObjectReplacementMap&&)=delete;
  // Borrow only for synchronous const operations; the callee must never mutate/destroy/free this view.
  const void* BorrowedMap()const{return &map_;}
  const object_map_abi::Node* Nodes()const{return nodes_.data();}
  size_t Size()const{return nodes_.size();}
  size_t BucketCount()const{return buckets_.size();}
 private:
  object_map_abi::Map map_{};
  std::vector<object_map_abi::Node> nodes_;
  std::vector<object_map_abi::NodeBase*> buckets_;
};
// Read-only SOURCE verification through real platform const find/iteration/hash.
bool VerifyObjectReplacementMap(void* implementation,const ObjectReplacementMap&,std::string*);
bool ConfigureObjectReplacementMaps(void* implementation,std::string*);
}

#pragma once
#include "platform/common/object_replacement_map_abi.h"
#include "platform/common/prepared_root_bindings.h"
#include <utility>
#include <vector>
#include <string>
namespace artdcevm::object_replacement {
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
  // Borrowed native-only lookup after verification; no allocation or platform
  // STL call. Keys may have moved during the operation's preceding GC refresh.
  bool Maps(void* source,void* destination)const noexcept;
  void* Replacement(void* source)const noexcept;
  bool CollectManagedRoots(const prepared_roots::Registrar&);
  bool CanRefreshManagedRoots(const prepared_roots::Resolver& r)const noexcept{return roots_.CanRefresh(r);}
  void RefreshManagedRoots(const prepared_roots::Resolver&)noexcept;
 private:
  friend bool VerifyObjectReplacementMap(const ObjectReplacementMap&,std::string*);
  void Rebuild()noexcept;
  prepared_roots::Bindings roots_;
  std::vector<std::pair<void*,void*>> entries_;
  object_map_abi::Map map_{};
  std::vector<object_map_abi::Node> nodes_;
  std::vector<object_map_abi::NodeBase*> buckets_;
  // One synchronous owner, like RefreshManagedRoots. Reused in final native
  // validation so duplicate-key detection never allocates during commit.
  mutable std::vector<uintptr_t> verification_keys_;
};
// Validate owned storage before lending it to the matched platform ABI. The
// loader verifies platform identities; host tests use an independent libc++
// oracle, and device commits exercise the real ART const-map consumer.
bool VerifyObjectReplacementMap(const ObjectReplacementMap&,std::string*);
}

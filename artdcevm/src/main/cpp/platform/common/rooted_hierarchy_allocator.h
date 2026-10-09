#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
namespace artdcevm::rooted_hierarchy {
// Slots are actual GC-updated ART/JNI roots owned by the surrounding source
// allocation lifetime. Object identities must be freshly decoded after calls.
using Root=void*;
struct Target {void* owner;Root klass,cache;bool structural;};
struct Replacement {Root old_class,new_class;};
class Access {
 public:
  virtual ~Access()=default;
  virtual Root Copy(Root)=0;
  virtual Root Empty()=0;
  virtual void* Get(Root)const=0;
  virtual void Set(Root,void*)=0;
  virtual Root Loader(Root)=0;
  virtual Root Cache(Root)=0;
  virtual uint16_t ClassIndex(Root)const=0;
  virtual std::string RequestedParent(Root cache,uint16_t)=0;
  virtual Root Resolve(const std::string&,Root loader)=0;
  virtual bool Subclass(Root child,Root parent)const=0;
  // Borrow the result slot before the real allocator: an Ensure*Ids boundary
  // can populate it even if the allocator subsequently returns null/OOM.
  virtual void* Allocate(void* owner,Root old_class,Root selected_parent,
      Root cache,uint16_t index,Root result_slot)=0;
  virtual bool ValidateReplacement(Root old_class,Root result,Root parent,Root cache)const=0;
  virtual void Fail(const char*)=0;
};
class Allocator {
 public:
  bool Prepare(std::vector<Target> targets){
    if(!targets_.empty()||!replacements_.empty()||targets.empty())return false;
    for(const auto& target:targets)if(!target.owner||!target.klass||!target.cache)return false;
    targets_=std::move(targets);return true;
  }
  void* Allocate(Access&,void* owner,Root old_class,Root cache,uint16_t index);
  const std::vector<Replacement>& Replacements()const{return replacements_;}
  void Clear(){replacements_.clear();targets_.clear();failed_=false;}
 private:
  bool failed_=false;
  std::vector<Target> targets_;
  std::vector<Replacement> replacements_;
};
}

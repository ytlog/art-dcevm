#pragma once
#include "platform/common/prepared_root_bindings.h"
#include <jni.h>
#include <vector>
#include <algorithm>
#include <cstdint>
namespace artdcevm::prepared_roots {
// One transaction owner converts independently calibrated native managed
// identities into real strong globals in the refundable Collected window.
// Once frozen, final resolution updates only already allocated pointer slots.
class Registry final {
 public:
  struct Access {
   void* owner=nullptr;
   jobject (*root)(void*,void*)=nullptr;
   bool (*decode)(void*,jobject,void**)=nullptr;
   void (*release)(void*,jobject)=nullptr;
  };
  explicit Registry(Access access):access_(access){}
  Registry(const Registry&)=delete;Registry& operator=(const Registry&)=delete;
  bool Add(void* managed,size_t* slot){
   if(frozen_||!managed||!slot||!access_.root||!access_.decode||!access_.release)return false;
   for(size_t i=0;i<roots_.size();++i){void* live=nullptr;
    if(!access_.decode(access_.owner,roots_[i],&live)||!live)return false;
    if(live==managed){*slot=i;return true;}
   }
   jobject global=access_.root(access_.owner,managed);if(!global)return false;
   void* live=nullptr;if(!access_.decode(access_.owner,global,&live)||live!=managed){access_.release(access_.owner,global);return false;}
   try{roots_.push_back(global);}catch(...){access_.release(access_.owner,global);throw;}
   *slot=roots_.size()-1;return true;
  }
  bool Freeze(){if(frozen_||roots_.empty())return false;resolved_.resize(roots_.size());index_.resize(roots_.size());frozen_=true;return true;}
  bool Resolve() noexcept {
   Invalidate();
   if(!frozen_||resolved_.size()!=roots_.size()||!access_.decode)return false;
   for(size_t i=0;i<roots_.size();++i){void* live=nullptr;if(!access_.decode(access_.owner,roots_[i],&live)||!live)return false;resolved_[i]=live;index_[i]={reinterpret_cast<uintptr_t>(live),i};}
   std::sort(index_.begin(),index_.end());resolved_valid_=true;
   return true;
  }
  // Resolve only while the owner proves the root snapshot cannot move. Drop it
  // before any possible safepoint. These APIs grant no thread/GC ownership.
  void Invalidate() noexcept {resolved_valid_=false;}
  // Consumers may bind additional pointer slots after global registration is
  // closed. An unknown identity refuses; it never creates/releases a root,
  // calls a decoder, or allocates. The latest complete Resolve is mandatory.
  Registrar Existing() noexcept {return {this,FindRoot};}
  Registrar Collect() noexcept {return {this,AddRoot};}
  Resolver Final() noexcept {return {this,GetRoot};}
  void Clear(){Invalidate();for(auto root:roots_)access_.release(access_.owner,root);roots_.clear();resolved_.clear();index_.clear();frozen_=false;}
  size_t Size()const noexcept{return roots_.size();}
 private:
  static bool AddRoot(void* owner,void* managed,size_t* slot){return static_cast<Registry*>(owner)->Add(managed,slot);}
  static bool FindRoot(void* owner,void* managed,size_t* slot) noexcept {
   const auto& registry=*static_cast<Registry*>(owner);
   if(!registry.frozen_||!registry.resolved_valid_||!managed||!slot)return false;
   const uintptr_t identity=reinterpret_cast<uintptr_t>(managed);
   const auto at=std::lower_bound(registry.index_.begin(),registry.index_.end(),std::pair<uintptr_t,size_t>{identity,0});
   if(at==registry.index_.end()||at->first!=identity)return false;
   *slot=at->second;return true;
  }
  static void* GetRoot(void* owner,size_t slot) noexcept {
   const auto& registry=*static_cast<Registry*>(owner);return registry.frozen_&&registry.resolved_valid_&&slot<registry.resolved_.size()?registry.resolved_[slot]:nullptr;
  }
  Access access_;std::vector<jobject> roots_;std::vector<void*> resolved_;
  std::vector<std::pair<uintptr_t,size_t>> index_;bool frozen_=false,resolved_valid_=false;
};
}

#pragma once
#include "platform/common/prepared_root_bindings.h"
#include <jni.h>
#include <vector>
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
  bool Freeze(){if(frozen_||roots_.empty())return false;resolved_.resize(roots_.size());frozen_=true;return true;}
  bool Resolve() noexcept {
   if(!frozen_||resolved_.size()!=roots_.size()||!access_.decode)return false;
   for(size_t i=0;i<roots_.size();++i){void* live=nullptr;if(!access_.decode(access_.owner,roots_[i],&live)||!live)return false;resolved_[i]=live;}
   return true;
  }
  Registrar Collect() noexcept {return {this,AddRoot};}
  Resolver Final() noexcept {return {this,GetRoot};}
  void Clear(){for(auto root:roots_)access_.release(access_.owner,root);roots_.clear();resolved_.clear();frozen_=false;}
  size_t Size()const noexcept{return roots_.size();}
 private:
  static bool AddRoot(void* owner,void* managed,size_t* slot){return static_cast<Registry*>(owner)->Add(managed,slot);}
  static void* GetRoot(void* owner,size_t slot) noexcept {
   const auto& registry=*static_cast<Registry*>(owner);return registry.frozen_&&slot<registry.resolved_.size()?registry.resolved_[slot]:nullptr;
  }
  Access access_;std::vector<jobject> roots_;std::vector<void*> resolved_;bool frozen_=false;
};
}

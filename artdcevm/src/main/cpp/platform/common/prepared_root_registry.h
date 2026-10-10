#pragma once
#include "platform/common/prepared_root_bindings.h"
#include <jni.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <limits>
namespace artdcevm::prepared_roots {
// One transaction owner registers independently calibrated native managed
// identities in actual GC-updated storage in the Collected window.
// Once frozen, final resolution updates only already allocated pointer slots.
class Registry final {
 public:
  struct Access {
   void* owner=nullptr;
   jobject (*root)(void*,void*)=nullptr;
   bool (*decode)(void*,jobject,void**)=nullptr;
   void (*release)(void*,jobject)=nullptr;
   // Native bounded storage is prepared before any managed pointer borrow.
   // epoch identifies one actual no-safepoint insertion window, never a GC.
   bool (*insert)(void*,void*,size_t*)=nullptr;
   bool (*read)(void*,size_t,void**)=nullptr;
   void (*clear)(void*)=nullptr;
   uint64_t (*epoch)(void*)=nullptr;
  };
  explicit Registry(Access access):access_(access){}
  Registry(const Registry&)=delete;Registry& operator=(const Registry&)=delete;
  bool BindNative(void* owner,bool (*insert)(void*,void*,size_t*),bool (*read)(void*,size_t,void**),void (*clear)(void*),uint64_t (*epoch)(void*)){
   if(frozen_||!roots_.empty()||!owner||!insert||!read||!clear||!epoch)return false;
   access_={owner,nullptr,nullptr,nullptr,insert,read,clear,epoch};return true;
  }
  // Storage ownership only; actual Runnable/slot permission stays with callbacks.
  bool IsNativeBoundTo(const void* owner)const noexcept {return owner&&access_.owner==owner&&access_.insert&&access_.read&&access_.clear&&access_.epoch;}
  bool Reserve(size_t capacity){
   if(frozen_||!roots_.empty()||!capacity||capacity>SIZE_MAX/4)return false;
   if(access_.insert&&(!access_.read||!access_.clear||!access_.epoch))return false;
   size_t buckets=1;while(buckets<capacity*2){if(buckets>SIZE_MAX/2)return false;buckets*=2;}
   capacity=buckets/2;roots_.reserve(capacity);resolved_.reserve(capacity);index_.reserve(capacity);
   insertion_.assign(buckets,{0,SIZE_MAX});capacity_=capacity;insertion_epoch_=0;return true;
  }
  bool Add(void* managed,size_t* slot){
   if(frozen_||!managed||!slot)return false;
   if(access_.insert){
    const auto epoch=access_.epoch(access_.owner);
    if(!epoch||(insertion_epoch_&&insertion_epoch_!=epoch))return false;
    if(roots_.size()==capacity_){
     const size_t next=capacity_?capacity_*2:64;
     if(next<capacity_||next>SIZE_MAX/4)return false;
     roots_.reserve(next);resolved_.reserve(next);index_.reserve(next);
     std::vector<std::pair<uintptr_t,size_t>> grown(next*2,{0,SIZE_MAX});
     for(const auto& entry:insertion_)if(entry.second!=SIZE_MAX){size_t at=Hash(entry.first)&(grown.size()-1);while(grown[at].second!=SIZE_MAX)at=(at+1)&(grown.size()-1);grown[at]=entry;}
     insertion_.swap(grown);capacity_=next;
    }
    const uintptr_t key=reinterpret_cast<uintptr_t>(managed);
    size_t bucket=Hash(key)&(insertion_.size()-1);
    while(insertion_[bucket].second!=SIZE_MAX){
     if(insertion_[bucket].first==key){*slot=insertion_[bucket].second;return true;}
     bucket=(bucket+1)&(insertion_.size()-1);
    }

    size_t stored=SIZE_MAX;void* live=nullptr;
    if(!access_.insert(access_.owner,managed,&stored)||stored==SIZE_MAX||
       !access_.read(access_.owner,stored,&live)||live!=managed)return false;
    // All storage was reserved in an ordinary phase. No allocation here.
    roots_.push_back({nullptr,stored});*slot=roots_.size()-1;
    insertion_[bucket]={key,*slot};insertion_epoch_=epoch;return true;
   }
   if(!access_.root||!access_.decode||!access_.release)return false;
   for(size_t i=0;i<roots_.size();++i){void* live=nullptr;
    if(!access_.decode(access_.owner,roots_[i].global,&live)||!live)return false;
    if(live==managed){*slot=i;return true;}
   }
   jobject global=access_.root(access_.owner,managed);if(!global)return false;
   void* live=nullptr;if(!access_.decode(access_.owner,global,&live)||live!=managed){access_.release(access_.owner,global);return false;}
   try{roots_.push_back({global,SIZE_MAX});}catch(...){access_.release(access_.owner,global);throw;}
   *slot=roots_.size()-1;return true;
  }
  bool Freeze(){if(frozen_||roots_.empty())return false;resolved_.resize(roots_.size());index_.resize(roots_.size());frozen_=true;return true;}
  bool Resolve() noexcept {
   Invalidate();
   if(!frozen_||resolved_.size()!=roots_.size()||(!access_.read&&!access_.decode))return false;
   for(size_t i=0;i<roots_.size();++i){void* live=nullptr;if(!(access_.read?access_.read(access_.owner,roots_[i].native,&live):access_.decode(access_.owner,roots_[i].global,&live))||!live)return false;resolved_[i]=live;index_[i]={reinterpret_cast<uintptr_t>(live),i};}
   std::sort(index_.begin(),index_.end());
   resolved_valid_=true;
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
  void Clear(){Invalidate();if(access_.clear)access_.clear(access_.owner);else for(auto root:roots_)access_.release(access_.owner,root.global);roots_.clear();resolved_.clear();index_.clear();insertion_.clear();capacity_=0;insertion_epoch_=0;frozen_=false;if(access_.insert)access_={};}
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
  static size_t Hash(uintptr_t value)noexcept{value^=value>>33;value*=UINT64_C(0xff51afd7ed558ccd);value^=value>>33;return size_t(value);}
  struct Root {jobject global;size_t native;};
  Access access_;std::vector<Root> roots_;std::vector<void*> resolved_;
  std::vector<std::pair<uintptr_t,size_t>> insertion_;size_t capacity_=0;uint64_t insertion_epoch_=0;
  std::vector<std::pair<uintptr_t,size_t>> index_;bool frozen_=false,resolved_valid_=false;
};
}

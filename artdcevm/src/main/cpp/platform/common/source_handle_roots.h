#pragma once
#include "platform/common/modern_field_storage.h"
#include "platform/abi/arm64/android-common/handle_roots.h"
#include <memory>
#include <vector>
#include <map>
#include <cstring>
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::source_handle_roots {
class Scope;
class Service final {
 public:
  // Bind in ordinary Native JNI after the complete real field/card oracle.
  // Binding does not authorize root publication or hierarchy mutation.
  static bool Bind(HookEngine&,const modern_field_storage::Service&,Service*,std::string*);
  // Production uses the independently observed JNI environment declaration
  // instead of assuming Thread TLS self/env positions from its card slot.
  static bool Bind(HookEngine&,const modern_field_storage::Service&,
                   const runnable_jni::Context& actual_native,Service*,std::string*);
  // Called at the actual named AllocateNewClassObject hook with its old-Class
  // Handle argument and the owner's independently rooted public original Class.
  // Callback must run in its already established live Runnable Context.
  bool ObserveAllocatorHandle(const runnable_jni::Context&,jobject strong_original,const void* source_handle,std::string*);
  // Borrow a live source slot only after actual walker calibration; output may
  // not survive a safepoint unless immediately copied into an owned Scope.
  // Borrow only a caller-owned public strong global on the independently
  // matched current Runnable JNI thread. This grants no private Scope access.
  // Only for a privately retained, previously checked strong global. The
  // owner guarantees its kind/lifetime; direct named decode makes no JNI call
  // and grants no HandleScope or hierarchy permission.
  bool BorrowOwnedStrongGlobal(const runnable_jni::Context&,jobject,void**)const noexcept;
  bool BorrowStrongGlobal(const runnable_jni::Context&,jobject,void**)const;
  bool BorrowHandle(const runnable_jni::Context&,const void* slot,void** output)const;
  bool ReadyForSourceAccess()const;
  bool Calibrated()const{return calibrated_.load(std::memory_order_acquire);}
  std::unique_ptr<Scope> CreateScope(uint32_t count,std::string*)const;
 private:
  friend class Scope;
  friend class Pool;
  bool Runnable(void*)const;
  bool Environment(const runnable_jni::Context&)const noexcept;
  void Visit(void*,abi::arm64::android_common::HandleRootVisitor*)const;
  static bool BindImpl(HookEngine&,const modern_field_storage::Service&,
                       const runnable_jni::Context*,Service*,std::string*);
  bool Contains(void*,const void*,uint32_t,size_t references=0,void** owner=nullptr)const;
  bool CalibrateLayout(void*,const void*,uint32_t);
  bool CheckNested(void*,uint32_t);
  std::unique_ptr<Scope> CreateUnchecked(void*,uint32_t)const;
  std::unique_ptr<Scope> CreateAnchored(Scope&,uint32_t,std::string*)const;
  abi::arm64::android11::DecodeJObject decode_=nullptr;
  abi::arm64::android11::CurrentThread current_=nullptr;
  abi::arm64::android_common::HandleVisitRoots visit_=nullptr;
  abi::arm64::android_common::HandleVisitRootsSigned visit_signed_=nullptr;
  void* vm_=nullptr;bool contextual_=false;
  size_t prefix_=0,top_=0,references_=0;
  unsigned shift_=0;uint32_t runnable_=0;
  // Exact current Thread and readable prefix independently checked at Native
  // binding. A matching named Current establishes that Thread's live lifetime;
  // cached bounds permit an early state-only refusal, never a read permission.
  void* bound_self_=nullptr;size_t bound_prefix_=0;
  std::atomic<bool> calibrated_{false};
};
// A GC-updated compressed root slot stays stable; Get returns only a borrowed
// pointer until the next potential suspension. Scopes are strictly LIFO and
// stay on the same actual Runnable thread through destruction.
class Scope final {
 public:
  ~Scope();
  Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
  bool ReadyForSourceAccess()const{return service_.Runnable(self_);}
  void* Slot(size_t);
  void* Get(size_t);
  void Set(size_t,void*);
 private:
  friend class Service;
  friend class Pool;
  Scope(const Service&,void*,uint32_t,Scope* anchor=nullptr);
  const Service& service_;void* self_;uint32_t count_;Scope* anchor_;
  std::unique_ptr<unsigned char[]> storage_;
};
// Shared roots retain a persistent owned anchor published before original Run.
// Later chunks link below that anchor, keeping original temporary source scopes
// and their saved previous pointer intact. Reverse teardown removes children
// before the anchor after all original temporary scopes have returned.
// Independent final owner. Window must prove the real source native/exclusive
// GC reservation; Read consumes pre-registered immutable root slots only.
class StoppedReader {
 public:
 virtual ~StoppedReader()=default;
 virtual bool FinalRootWindow()const noexcept=0;
 virtual bool ReadStoppedRoot(const void*,void**)const noexcept=0;
};
class Pool final {
 public:
  explicit Pool(const Service& service):service_(service){}
  bool UsesService(const Service& service)const noexcept{return &service_==&service;}
  ~Pool(){while(!scopes_.empty())scopes_.pop_back();}
  bool ReadyForSourceAccess()const{return service_.ReadyForSourceAccess()&&(scopes_.empty()||scopes_.front()->ReadyForSourceAccess());}
  Pool(const Pool&)=delete;Pool& operator=(const Pool&)=delete;
  bool Read(const void* slot,void** output)const{
    if(!output||!ReadyForSourceAccess())return false;
    const auto entry=FindOwned(slot);if(entry==ranges_.end())return false;
    *output=entry->second.scope->Get((uintptr_t(slot)-entry->first)/4);return true;
  }
  bool ReadStopped(const StoppedReader& owner,const void* slot,void** output)const noexcept{
    if(!output||!owner.FinalRootWindow())return false;
    const auto entry=FindOwned(slot);if(entry==ranges_.end())return false;
    uint32_t value=0;std::memcpy(&value,slot,4);*output=reinterpret_cast<void*>(uintptr_t(value));return true;
  }
  bool Write(void* slot,void* object){
    if(!ReadyForSourceAccess()||uintptr_t(object)>UINT32_MAX)return false;
    for(size_t i=0;i<scopes_.size();++i){const uintptr_t start=uintptr_t(scopes_[i]->Slot(0)),address=uintptr_t(slot);
      const size_t count=i+1==scopes_.size()?used_:kChunk;
      if(address>=start&&(address-start)%4==0&&(address-start)/4<count){scopes_[i]->Set((address-start)/4,object);return true;}}
    return false;
  }
  void* Add(void* object,std::string* error){
    if(scopes_.empty()||used_==kChunk){auto scope=scopes_.empty()?service_.CreateScope(kChunk,error):service_.CreateAnchored(*scopes_.front(),kChunk,error);if(!scope)return nullptr;scopes_.push_back(std::move(scope));used_=0;
      try{ranges_.emplace(uintptr_t(scopes_.back()->Slot(0)),Range{scopes_.back().get(),0});}
      catch(...){scopes_.pop_back();used_=kChunk;throw;}}
    auto& scope=*scopes_.back();void* slot=scope.Slot(used_);scope.Set(used_++,object);ranges_.find(uintptr_t(scope.Slot(0)))->second.used=used_;return slot;
  }
 // Enumerates opaque used slots only in the ordinary Runnable phase. Visitor
 // may register roots/safepoint, so it must read each slot afresh itself.
 template<class Visitor> bool VisitUsedSlots(Visitor visitor)const{
  if(!ReadyForSourceAccess())return false;
  for(size_t i=0;i<scopes_.size();++i){const size_t count=i+1==scopes_.size()?used_:kChunk;
   for(size_t n=0;n<count;++n)if(!visitor(scopes_[i]->Slot(n)))return false;}
  return true;
 }
 private:
  struct Range {Scope* scope;size_t used;};
  using Ranges=std::map<uintptr_t,Range>;
  Ranges::const_iterator FindOwned(const void* slot)const noexcept{
    const auto address=uintptr_t(slot);auto at=ranges_.upper_bound(address);
    if(at==ranges_.begin())return ranges_.end();--at;
    if((address-at->first)%4||(address-at->first)/4>=at->second.used)return ranges_.end();return at;
  }
  Ranges ranges_;
  static constexpr uint32_t kChunk=32;
  const Service& service_;std::vector<std::unique_ptr<Scope>> scopes_;uint32_t used_=kChunk;
};
}

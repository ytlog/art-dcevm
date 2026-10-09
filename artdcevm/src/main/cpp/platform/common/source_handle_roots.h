#pragma once
#include "platform/common/modern_field_storage.h"
#include "platform/abi/arm64/android-common/handle_roots.h"
#include <memory>
#include <vector>
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::source_handle_roots {
class Scope;
class Service final {
 public:
  // Bind in ordinary Native JNI after the complete real field/card oracle.
  // Binding does not authorize root publication or hierarchy mutation.
  static bool Bind(HookEngine&,const modern_field_storage::Service&,Service*,std::string*);
  // Called at the actual named AllocateNewClassObject hook with its old-Class
  // Handle argument and the owner's independently rooted public original Class.
  // Callback must run in its already established live Runnable Context.
  bool ObserveAllocatorHandle(const runnable_jni::Context&,jobject strong_original,const void* source_handle,std::string*);
  // Borrow a live source slot only after actual walker calibration; output may
  // not survive a safepoint unless immediately copied into an owned Scope.
  bool BorrowHandle(const runnable_jni::Context&,const void* slot,void** output)const;
  bool ReadyForSourceAccess()const;
  bool Calibrated()const{return calibrated_.load(std::memory_order_acquire);}
  std::unique_ptr<Scope> CreateScope(uint32_t count,std::string*)const;
 private:
  friend class Scope;
  bool Runnable(void*)const;
  bool Contains(void*,const void*,uint32_t)const;
  bool CheckNested(void*,uint32_t);
  std::unique_ptr<Scope> CreateUnchecked(void*,uint32_t)const;
  abi::arm64::android11::DecodeJObject decode_=nullptr;
  abi::arm64::android11::CurrentThread current_=nullptr;
  abi::arm64::android_common::HandleVisitRoots visit_=nullptr;
  size_t prefix_=0,top_=0;
  unsigned shift_=0;uint32_t runnable_=0;
  std::atomic<bool> calibrated_{false};
};
// A GC-updated compressed root slot stays stable; Get returns only a borrowed
// pointer until the next potential suspension. Scopes are strictly LIFO and
// stay on the same actual Runnable thread through destruction.
class Scope final {
 public:
  ~Scope();
  Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
  void* Slot(size_t);
  void* Get(size_t);
  void Set(size_t,void*);
 private:
  friend class Service;
  Scope(const Service&,void*,uint32_t);
  const Service& service_;void* self_;uint32_t count_;
  std::unique_ptr<unsigned char[]> storage_;
};
// Shared allocator roots use stable opaque slots; destruction unlinks scopes
// explicitly in reverse publication order rather than relying on vector order.
class Pool final {
 public:
  explicit Pool(const Service& service):service_(service){}
  ~Pool(){while(!scopes_.empty())scopes_.pop_back();}
  bool ReadyForSourceAccess()const{return service_.ReadyForSourceAccess();}
  Pool(const Pool&)=delete;Pool& operator=(const Pool&)=delete;
  void* Add(void* object,std::string* error){
    if(scopes_.empty()||used_==kChunk){auto scope=service_.CreateScope(kChunk,error);if(!scope)return nullptr;scopes_.push_back(std::move(scope));used_=0;}
    auto& scope=*scopes_.back();void* slot=scope.Slot(used_);scope.Set(used_++,object);return slot;
  }
 private:
  static constexpr uint32_t kChunk=32;
  const Service& service_;std::vector<std::unique_ptr<Scope>> scopes_;uint32_t used_=kChunk;
};
}

#pragma once
#include "platform/abi/arm64/android11/thread_state.h"
#include "platform/abi/arm64/android13/thread_state.h"
#include "platform/abi/common/suspend_all.h"

namespace artdcevm::thread_suspend_service {
enum class Declaration {kLegacy16,kModern24};
class NativeScope;
class SuspendScope;
// A caller selects the independently calibrated Thread/profile declaration;
// SDK cannot identify Mainline/vendor state storage. This service never writes
// state or simulates ART mutator locks. Missing transitions are a capability gap.
class Service {
 public:
  template<class Finder> static bool Bind(Declaration declaration,Finder finder,Service* output) {
    if(!output || (declaration!=Declaration::kLegacy16 && declaration!=Declaration::kModern24)) return false;
    Service candidate;
    candidate.declaration_=declaration;
    candidate.construct_=reinterpret_cast<abi::common::SuspendConstruct>(finder(abi::common::kSuspendConstruct));
    candidate.destroy_=reinterpret_cast<abi::common::SuspendDestroy>(finder(abi::common::kSuspendDestroy));
    if(!candidate.construct_ || !candidate.destroy_) return false;
    if(declaration==Declaration::kModern24) {
      candidate.native_=reinterpret_cast<abi::arm64::android13::Transition>(finder(abi::arm64::android13::kToNative));
      candidate.runnable_=reinterpret_cast<abi::arm64::android13::Transition>(finder(abi::arm64::android13::kToRunnable));
      // Require a complete reversible pair. Legacy JniMethodStart also manages
      // cookies/local refs, so it is deliberately not a transition substitute.
      if(!candidate.native_ || !candidate.runnable_) candidate.native_=candidate.runnable_=nullptr;
    }
    *output=candidate;
    return true;
  }
  bool CanTransitionRunnable() const {return native_ && runnable_;}
  uint32_t RunnableState()const noexcept{return Runnable();}
  bool SameBinding(const Service& other)const noexcept {
    return declaration_==other.declaration_&&native_==other.native_&&runnable_==other.runnable_&&
      construct_==other.construct_&&destroy_==other.destroy_;
  }
  // self is the current thread's calibrated live native storage; arithmetic
  // bounds do not independently establish that a mapping is readable.
  bool State(const void* self,size_t extent,uint32_t* output) const {
    const auto address=reinterpret_cast<uintptr_t>(self);
    const size_t offset=declaration_==Declaration::kLegacy16 ? abi::arm64::android11::kThreadFlagsOffset : abi::arm64::android13::kThreadFlagsOffset;
    if(!construct_ || !self || !output || address%4 || extent<offset+4 || address>UINTPTR_MAX-offset-4) return false;
    const auto* flags=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(self)+offset);
    const uint32_t word=__atomic_load_n(flags,__ATOMIC_RELAXED);
    *output=word>>(declaration_==Declaration::kLegacy16 ? abi::arm64::android11::kStateShift : abi::arm64::android13::kStateShift);
    return true;
  }
  bool EnterNative(void* self,size_t extent,bool* transitioned) const {
    uint32_t state;
    if(!transitioned || !State(self,extent,&state)) return false;
    if(state==abi::arm64::android11::kNative) {*transitioned=false;return true;}
    if(state!=Runnable() || !CanTransitionRunnable()) return false;
    native_(self);*transitioned=true;return true;
  }
  bool RestoreRunnable(void* self) const {
    const auto address=reinterpret_cast<uintptr_t>(self);
    if(!self || !runnable_ || address%4 || address>UINTPTR_MAX-4) return false;
    runnable_(self);return true;
  }
  bool ConstructSuspend(void* storage,size_t extent,const char* cause,bool long_suspend) const {
    if(!construct_ || !destroy_ || !storage || extent<sizeof(abi::common::SuspendAll) ||
       reinterpret_cast<uintptr_t>(storage)>UINTPTR_MAX-sizeof(abi::common::SuspendAll) || !cause) return false;
    construct_(storage,cause,long_suspend);return true;
  }
  bool DestroySuspend(void* storage,size_t extent) const {
    if(!destroy_ || !storage || extent<sizeof(abi::common::SuspendAll) ||
       reinterpret_cast<uintptr_t>(storage)>UINTPTR_MAX-sizeof(abi::common::SuspendAll)) return false;
    destroy_(storage);return true;
  }
 private:
  uint32_t Runnable() const {return declaration_==Declaration::kLegacy16 ? abi::arm64::android11::kRunnable : abi::arm64::android13::kRunnable;}
  Declaration declaration_=Declaration::kLegacy16;
  abi::arm64::android13::Transition native_=nullptr,runnable_=nullptr;
  abi::common::SuspendConstruct construct_=nullptr;
  abi::common::SuspendDestroy destroy_=nullptr;
  friend class NativeScope;
  friend class SuspendScope;
};
// Only the current owning ART thread may create/destroy this scope. An already
// Native caller needs no transition; Runnable requires the matched named pair.
// Keep the loaded module alive. Copying the binding ensures restoration uses the
// same function even if an unrelated service variable is later rebound.
class NativeScope {
 public:
  NativeScope(const Service& service,void* self,size_t extent):service_(service),self_(self) {
    supported_=service_.EnterNative(self,extent,&transitioned_);
  }
  ~NativeScope() {if(transitioned_) service_.runnable_(self_);}
  bool Supported() const {return supported_;}
  NativeScope(const NativeScope&)=delete;
  NativeScope& operator=(const NativeScope&)=delete;
 private:
  Service service_;
  void* self_;
  bool supported_=false,transitioned_=false;
};
// Construct only after a supported NativeScope, before thread-list locks; destroy
// after releasing those locks, before NativeScope restores Runnable. The named
// ART scope acquires/releases exclusive mutator access, not this byte carrier.
class SuspendScope {
 public:
  SuspendScope(const Service& service,const char* cause,bool long_suspend=false):service_(service) {
    supported_=service_.ConstructSuspend(&storage_,sizeof(storage_),cause,long_suspend);
  }
  ~SuspendScope() {if(supported_) service_.destroy_(&storage_);}
  bool Supported() const {return supported_;}
  SuspendScope(const SuspendScope&)=delete;
  SuspendScope& operator=(const SuspendScope&)=delete;
 private:
  Service service_;
  abi::common::SuspendAll storage_{};
  bool supported_=false;
};
}

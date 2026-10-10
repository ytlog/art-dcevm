#pragma once
#include "api/hook_engine.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <array>
namespace artdcevm::native_field_factory_observer {
// Handle<T> is one pointer to the caller's source StackReference<T> slot.
// The extra-Handle factory body/argument meaning remains unproven: this typed
// adapter observes ART's original caller, never invokes it independently.
// Optional independently proven field normalization does not interpret Handle.
struct HandleCarrier {const void* slot;};
using Return=abi::arm64::android11::ReleaseObjectPointer;
using Factory=Return (*)(void* self,HandleCarrier,void* native_field,bool force);
struct Normalizer {
 void* owner=nullptr;
 // False preserves the consumer-produced actual pending exception and refuses
 // the original factory. No JNI/allocation/locks or hidden Handle interpretation.
 bool (*normalize)(void*,void*,HandleCarrier,void*,bool,void**) noexcept=nullptr;
};
struct Bindings {
 void* owner=nullptr;void* (*current)()=nullptr;
 // Original named root barrier, bounded readable slot, same actual Thread.
 // No JNI, allocation, stop, safepoint or field/cache writes in this callback.
 bool (*root)(void*,void* self,const void* slot,void** object)=nullptr;
};
struct Entry {void* field=nullptr;void* normalized_field=nullptr;bool normalization_invoked=false;size_t calls=0,false_calls=0;uintptr_t self=0,handle_slot=0,root_before=0,root_after=0,result=0;bool root_before_valid=false,root_after_valid=false;};
struct Trace {std::array<Entry,2> entries{};};
// Installation is not completion: a failed first original episode stays refused.
class EpisodeCompletion {
 enum class State {kNew,kIncomplete,kComplete};State state_=State::kNew;
 public:
 bool Attempt(){if(state_!=State::kNew)return false;state_=State::kIncomplete;return true;}
 bool Ready()const{return state_==State::kComplete;}
 void Complete(){if(state_==State::kIncomplete)state_=State::kComplete;}
};
inline const char* RootFalseFailure(const Entry& e){
 if(!e.field)return "field-null";if(!e.calls)return "original-not-called";
 if(e.false_calls!=e.calls)return "non-false-call";
 if(!e.root_before_valid)return "before-root-unproven";if(!e.root_after_valid)return "after-root-unproven";
 if(!e.result)return "original-result-null";if(!e.root_after)return "after-root-null";
 if(e.root_before!=e.root_after)return "root-address-changed";
 if(e.normalized_field!=e.field)return "field-normalized";return nullptr;
}
inline bool RootFalseProof(const Trace& trace){for(const auto& e:trace.entries)if(RootFalseFailure(e))return false;return true;}
inline bool DeclaringHandleProof(const Entry& entry,void* declaring,void* returned_mirror){
 return !RootFalseFailure(entry)&&declaring&&returned_mirror&&entry.root_after==uintptr_t(declaring)&&entry.result==uintptr_t(returned_mirror);
}
inline std::string Describe(const Entry& e){
 const char* failure=RootFalseFailure(e);
 return std::string("proof=")+(failure?failure:"complete")+
  ",field="+std::to_string(uintptr_t(e.field))+",normalized="+std::to_string(uintptr_t(e.normalized_field))+
  ",calls="+std::to_string(e.calls)+",false="+std::to_string(e.false_calls)+
  ",self="+std::to_string(e.self)+",slot="+std::to_string(e.handle_slot)+
  ",beforeValid="+std::to_string(e.root_before_valid)+",before="+std::to_string(e.root_before)+
  ",afterValid="+std::to_string(e.root_after_valid)+",after="+std::to_string(e.root_after)+",result="+std::to_string(e.result);
}

class Service {
 public:
  Service()=default;Service(const Service&)=delete;Service& operator=(const Service&)=delete;
  bool Bind(HookEngine&,const Bindings&,std::string*);
  // Caller must run these only in the existing actual exclusive-owned-roots
  // operation. No second SuspendAll or fake Thread transition is created.
  bool InstallAtExclusive(HookEngine&,const exclusive_owned_roots::Context&,std::string*);
  bool RemoveAtExclusive(HookEngine&,const exclusive_owned_roots::Context&,std::string*);
  // Bracket only an original public Java reflection call on this actual Thread.
  // Caller keeps both independently identified native fields' Class roots live.
  bool SetNormalizerAtExclusive(const Normalizer&,const exclusive_owned_roots::Context&,std::string*);
  bool NormalizerInstalled()const noexcept{return installed_==this&&token_&&original_&&normalizer_.normalize&&!poisoned_;}
  bool FourArgument()const noexcept{return four_argument_;}
  bool Begin(void* self,const std::array<void*,2>& native_fields,std::string*);
  bool End(Trace*,std::string*);
  bool Poisoned()const noexcept{return poisoned_;}
 private:
  static Return Observe(void*,HandleCarrier,void*,bool);
  static Return ObserveThree(void*,void*,bool);
  Return Forward(void*,HandleCarrier,void*,bool);
  Bindings bindings_{};void* target_=nullptr;void* token_=nullptr;void* original_=nullptr;Normalizer normalizer_{};bool four_argument_=false;
  Trace trace_{};void* self_=nullptr;bool poisoned_=false;
  inline static Service* installed_=nullptr;
  inline static thread_local Service* active_=nullptr;
};
static_assert(sizeof(HandleCarrier)==sizeof(void*));
}

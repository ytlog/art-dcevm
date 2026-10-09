#include "platform/common/native_final_write_listener.h"
#include "platform/common/native_receiver_guard.h"
#include <cstring>
#include "platform/abi/arm64/android-common/instrumentation.h"
#include <cstdlib>
#include <atomic>
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <mutex>
namespace artdcevm::final_write_guard {
namespace {
// The fixed 11/12, 13/14 and 15+ headers preserve both FieldWritten virtual
// positions and signatures. Other listener slots differ between declarations;
// ordinary final-only configuration registers FieldWritten (0x20). A proven
// receiver consumer adds DexPcMoved/FieldRead (0x18), whose positions/signatures
// are also fixed. Unregistered slots retain explicit invariant refusal.
struct ShadowFrame;
struct Handle {uint32_t* slot;};
struct OptionalFrame {const void* frame;bool engaged;};
class FieldOnlyListener {
 public:
 virtual ~FieldOnlyListener()=default;
 virtual void MethodEntered(void*,void*){std::abort();}
 virtual void MethodExited(void*,void*,OptionalFrame,Handle&){std::abort();}
 virtual void MethodExited(void*,void*,OptionalFrame,uint64_t&){std::abort();}
 virtual void MethodUnwind(void*,Handle,void*,uint32_t){std::abort();}
 virtual void DexPcMoved(void*t,Handle,void*m,uint32_t pc){if(!receivers)std::abort();receivers->CheckBytecode(t,m,pc);invoke_events.fetch_add(1,std::memory_order_relaxed);}
 virtual void FieldRead(void*t,Handle object,void*,uint32_t,void*f){if(!receivers)std::abort();ReceiverField(t,object,f,false);read_events.fetch_add(1,std::memory_order_relaxed);}
 virtual void FieldWritten(void* t,Handle object,void* m,uint32_t,void* f,Handle){if(!ReceiverField(t,object,f,true))guard->FieldWritten(t,m,f,true);}
 virtual void FieldWritten(void* t,Handle object,void* m,uint32_t,void* f,const uint64_t&){if(!ReceiverField(t,object,f,true))guard->FieldWritten(t,m,f);}
 virtual void ExceptionThrown(void*,Handle){std::abort();}
 virtual void ExceptionHandled(void*,Handle){std::abort();}
 virtual void Branch(void*,void*,uint32_t,int32_t){std::abort();}
 virtual void WatchedFramePop(void*,const ShadowFrame&){std::abort();}
 bool ReceiverField(void*t,Handle object,void*f,bool write){if(!receivers)return false;uint32_t reference=0;if(object.slot)std::memcpy(&reference,object.slot,4);if(write)write_events.fetch_add(1,std::memory_order_relaxed);return receivers->CheckCompressed(write?receiver_guard::Path::BytecodeWrite:receiver_guard::Path::BytecodeRead,t,f,reference,nullptr,true);}
 Guard* guard=nullptr;receiver_guard::Guard* receivers=nullptr;
 std::atomic<uint64_t> invoke_events{0},read_events{0},write_events{0};
};
// Last vtable callback has no role in kFieldWritten; define its exact reference
// ABI through an opaque source type rather than an object field declaration.
FieldOnlyListener listener;
using LegacyAdd=void(*)(void*,void*,uint32_t);
using BoolAdd=void(*)(void*,void*,uint32_t,bool);
using TypedAdd=abi::arm64::android_common::TypedAddListener;
using Deopt=void(*)(void*,const char*);
enum class AddKind {None,Legacy,Bool,Typed};
using MethodDeopt=void(*)(void*,void*);
void* instrumentation=nullptr;void* add=nullptr;Deopt deopt=nullptr;MethodDeopt method_deopt=nullptr;
AddKind kind=AddKind::None;bool installed=false,receiver_installed=false;std::mutex configuration;
constexpr char kEnable[]="_ZN3art15instrumentation15Instrumentation20EnableDeoptimizationEv";
constexpr char kDisable[]="_ZN3art15instrumentation15Instrumentation21DisableDeoptimizationEPKc";
void* disable_original=nullptr;void* enable_original=nullptr;std::atomic<void*> enabled_receiver{nullptr};bool observer_installed=false,observer_attempted=false;bool legacy=false;
void Enabled(void* receiver){reinterpret_cast<void(*)(void*)>(enable_original)(receiver);enabled_receiver.store(receiver,std::memory_order_release);}
void Disabled(void* receiver,const char* key){
 reinterpret_cast<void(*)(void*,const char*)>(disable_original)(receiver,key);
 void* expected=receiver;enabled_receiver.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
}
constexpr char kKey[]="artdcevm-final-bytecode-guard";
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
bool ObserveLegacyDeoptimizationStartup(HookEngine& hooks,std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 void* target=hooks.FindSymbol("libart.so",kEnable);if(!target)return true;
 void* disable=hooks.FindSymbol("libart.so",kDisable);
 if(!disable)return Fail(error,"Legacy final guard lacks named DisableDeoptimization lifetime observation");
 if(observer_installed)return true;
 if(observer_attempted)return Fail(error,"Legacy deoptimization observer installation failed; restart process");
 using namespace abi::arm64::android_common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!construct||!destroy)return Fail(error,"Legacy final guard lacks named startup suspend capability");
 observer_attempted=true;
 SuspendAll storage{};construct(&storage,"artdcevm final guard enable observation",false);
 auto token=hooks.Install(target,reinterpret_cast<void*>(Enabled),&enable_original,error);
 if(token&&!enable_original)std::abort();
 void* disable_token=token?hooks.Install(disable,reinterpret_cast<void*>(Disabled),&disable_original,error):nullptr;
 if(disable_token&&!disable_original)std::abort();
 observer_installed=token&&enable_original&&disable_token&&disable_original;
 if(token&&!disable_token)hooks.Remove(token,error);
 destroy(&storage);return observer_installed||Fail(error,"Legacy deoptimization observer could not install before plugin initialization");
}
bool BindListener(HookEngine& hooks,void* receiver,Guard& guard,std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!receiver)return Fail(error,"Final guard lacks independently observed Instrumentation receiver");
 if(listener.guard){return (listener.guard==&guard&&instrumentation==receiver&&
     (!legacy||enabled_receiver.load(std::memory_order_acquire)==receiver))||
   Fail(error,"Final guard listener already belongs to another process owner");}
 auto target=hooks.FindSymbol("libart.so",abi::arm64::android_common::kTypedAddListener);
 AddKind selected=AddKind::Typed;
 if(!target){target=hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb");selected=AddKind::Bool;}
 if(!target){target=hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj");selected=AddKind::Legacy;}
 auto route=reinterpret_cast<Deopt>(hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc"));
 if(!target||!route)return Fail(error,"Final guard lacks named AddListener or interpreter deoptimization route");
 if(hooks.FindSymbol("libart.so",kEnable)&&(!observer_installed||enabled_receiver.load(std::memory_order_acquire)!=receiver))
  return Fail(error,"Legacy final guard has not observed actual EnableDeoptimization for this Instrumentation receiver");
 method_deopt=reinterpret_cast<MethodDeopt>(hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation10DeoptimizeEPNS_9ArtMethodE"));
 listener.guard=&guard;instrumentation=receiver;add=target;kind=selected;deopt=route;legacy=hooks.FindSymbol("libart.so",kEnable)!=nullptr;return true;
}
bool InstallListenerAtExclusiveBoundary(std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!listener.guard||!add||!deopt||!instrumentation)return Fail(error,"Final guard listener was not bound");
 if(legacy&&enabled_receiver.load(std::memory_order_acquire)!=instrumentation)return Fail(error,"Legacy deoptimization enabled witness expired before listener installation");
 if(installed)return true;
 constexpr uint32_t kFieldWritten=0x20;
 switch(kind){
  case AddKind::Typed:reinterpret_cast<TypedAdd>(add)(instrumentation,&listener,kFieldWritten,
    abi::arm64::android_common::ListenerType::kRegularListener);break;
  case AddKind::Bool:reinterpret_cast<BoolAdd>(add)(instrumentation,&listener,kFieldWritten,false);break;
  case AddKind::Legacy:reinterpret_cast<LegacyAdd>(add)(instrumentation,&listener,kFieldWritten);break;
  default:return Fail(error,"Final guard listener declaration missing");
 }
 installed=true;return true;
}
bool BindReceiverEvents(void*receiver,receiver_guard::Guard&guard,std::string*error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!installed||!receiver||receiver!=instrumentation||!listener.guard||!add||!deopt||(legacy&&enabled_receiver.load(std::memory_order_acquire)!=receiver))return Fail(error,"Receiver listener lacks the exact installed process Instrumentation owner");
 if(listener.receivers)return listener.receivers==&guard||Fail(error,"Receiver events already belong to another permanent guard");
 listener.receivers=&guard;return true;
}
bool InstallReceiverEventsAtExclusiveBoundary(std::string*error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!installed||!listener.receivers||!add||!instrumentation||(legacy&&enabled_receiver.load(std::memory_order_acquire)!=instrumentation))return Fail(error,"Receiver events have no live exact Instrumentation binding");
 if(receiver_installed)return true;constexpr uint32_t additional=0x18;
 switch(kind){
  case AddKind::Typed:reinterpret_cast<TypedAdd>(add)(instrumentation,&listener,additional,abi::arm64::android_common::ListenerType::kRegularListener);break;
  case AddKind::Bool:reinterpret_cast<BoolAdd>(add)(instrumentation,&listener,additional,false);break;
  case AddKind::Legacy:reinterpret_cast<LegacyAdd>(add)(instrumentation,&listener,additional);break;
  default:return Fail(error,"Receiver event source AddListener declaration missing");
 }receiver_installed=true;return true;
}
bool ReceiverListenerProven(const receiver_guard::Guard&guard)noexcept{
 std::lock_guard<std::mutex> lock(configuration);
 return installed&&receiver_installed&&listener.receivers==&guard&&guard.Proven()&&listener.invoke_events.load(std::memory_order_relaxed)&&listener.read_events.load(std::memory_order_relaxed)&&listener.write_events.load(std::memory_order_relaxed)&&(!legacy||enabled_receiver.load(std::memory_order_acquire)==instrumentation);
}
bool PrepareProofMethodAtExclusiveBoundary(void* method,std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!installed||!method||!method_deopt||
    (legacy&&enabled_receiver.load(std::memory_order_acquire)!=instrumentation))
  return Fail(error,"Final write fixture lacks named method-only deoptimization capability");
 method_deopt(instrumentation,method);return true;
}
bool ListenerProven() noexcept {
 std::lock_guard<std::mutex> lock(configuration);
 return installed&&listener.guard&&listener.guard->Proven()&&(!legacy||enabled_receiver.load(std::memory_order_acquire)==instrumentation);
}
bool ListenerProven(const Guard& guard) noexcept {
 std::lock_guard<std::mutex> lock(configuration);
 return installed&&listener.guard==&guard&&guard.Proven()&&(!legacy||enabled_receiver.load(std::memory_order_acquire)==instrumentation);
}
void RetainInterpretationAtCommit(){
 // Exclusive mutator ownership prevents callback execution while this key is
 // reasserted. Do not hold the configuration mutex across ART commit services.
 if(!installed||!listener.guard||!listener.guard->Proven()||!deopt||!instrumentation||
    (legacy&&enabled_receiver.load(std::memory_order_acquire)!=instrumentation))std::abort();
 deopt(instrumentation,kKey);
}
#ifdef ARTDCEVM_TESTING
void NotifyWrittenForTesting(bool reference,void* thread,void* method,void* field){
 if(reference)listener.FieldWritten(thread,{},method,0,field,Handle{});
 else {const uint64_t value=0;listener.FieldWritten(thread,{},method,0,field,value);}
}
#endif
} // namespace artdcevm::final_write_guard

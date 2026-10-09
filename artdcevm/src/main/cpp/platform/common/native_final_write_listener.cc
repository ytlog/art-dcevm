#include "platform/common/native_final_write_listener.h"
#include "platform/abi/arm64/android-common/instrumentation.h"
#include <cstdlib>
#include <mutex>
namespace artdcevm::final_write_guard {
namespace {
// The fixed 11/12, 13/14 and 15+ headers preserve both FieldWritten virtual
// positions and signatures. Other listener slots differ between declarations;
// this restricted listener registers ONLY kFieldWritten (0x20), so none of those
// slots may be invoked. This is not a general InstrumentationListener adapter.
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
 virtual void DexPcMoved(void*,Handle,void*,uint32_t){std::abort();}
 virtual void FieldRead(void*,Handle,void*,uint32_t,void*){std::abort();}
 virtual void FieldWritten(void* t,Handle,void* m,uint32_t,void* f,Handle){guard->FieldWritten(t,m,f);}
 virtual void FieldWritten(void* t,Handle,void* m,uint32_t,void* f,const uint64_t&){guard->FieldWritten(t,m,f);}
 virtual void ExceptionThrown(void*,Handle){std::abort();}
 virtual void ExceptionHandled(void*,Handle){std::abort();}
 virtual void Branch(void*,void*,uint32_t,int32_t){std::abort();}
 virtual void WatchedFramePop(void*,const ShadowFrame&){std::abort();}
 Guard* guard=nullptr;
};
// Last vtable callback has no role in kFieldWritten; define its exact reference
// ABI through an opaque source type rather than an object field declaration.
FieldOnlyListener listener;
using LegacyAdd=void(*)(void*,void*,uint32_t);
using BoolAdd=void(*)(void*,void*,uint32_t,bool);
using TypedAdd=abi::arm64::android_common::TypedAddListener;
using Deopt=void(*)(void*,const char*);
enum class AddKind {None,Legacy,Bool,Typed};
void* instrumentation=nullptr;void* add=nullptr;Deopt deopt=nullptr;
AddKind kind=AddKind::None;bool installed=false;std::mutex configuration;
constexpr char kKey[]="artdcevm-final-bytecode-guard";
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
bool BindListener(HookEngine& hooks,void* receiver,Guard& guard,std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!receiver)return Fail(error,"Final guard lacks independently observed Instrumentation receiver");
 if(listener.guard){return (listener.guard==&guard&&instrumentation==receiver)||
   Fail(error,"Final guard listener already belongs to another process owner");}
 auto target=hooks.FindSymbol("libart.so",abi::arm64::android_common::kTypedAddListener);
 AddKind selected=AddKind::Typed;
 if(!target){target=hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb");selected=AddKind::Bool;}
 if(!target){target=hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj");selected=AddKind::Legacy;}
 auto route=reinterpret_cast<Deopt>(hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc"));
 if(!target||!route)return Fail(error,"Final guard lacks named AddListener or interpreter deoptimization route");
 listener.guard=&guard;instrumentation=receiver;add=target;kind=selected;deopt=route;return true;
}
bool InstallListenerAtExclusiveBoundary(std::string* error){
 std::lock_guard<std::mutex> lock(configuration);
 if(!listener.guard||!add||!deopt||!instrumentation)return Fail(error,"Final guard listener was not bound");
 if(installed)return true;
 constexpr uint32_t kFieldWritten=0x20;
 switch(kind){
  case AddKind::Typed:reinterpret_cast<TypedAdd>(add)(instrumentation,&listener,kFieldWritten,
    abi::arm64::android_common::ListenerType::kRegularListener);break;
  case AddKind::Bool:reinterpret_cast<BoolAdd>(add)(instrumentation,&listener,kFieldWritten,false);break;
  case AddKind::Legacy:reinterpret_cast<LegacyAdd>(add)(instrumentation,&listener,kFieldWritten);break;
  default:return Fail(error,"Final guard listener declaration missing");
 }
 deopt(instrumentation,kKey);installed=true;return true;
}
bool ListenerProven() noexcept {
 std::lock_guard<std::mutex> lock(configuration);
 return installed&&listener.guard&&listener.guard->Proven();
}
void RetainInterpretationAtCommit(){
 // Exclusive mutator ownership prevents callback execution while this key is
 // reasserted. Do not hold the configuration mutex across ART commit services.
 if(!installed||!listener.guard||!listener.guard->Proven()||!deopt||!instrumentation)std::abort();
 deopt(instrumentation,kKey);
}
#ifdef ARTDCEVM_TESTING
void NotifyWrittenForTesting(bool reference,void* thread,void* method,void* field){
 if(reference)listener.FieldWritten(thread,{},method,0,field,Handle{});
 else {const uint64_t value=0;listener.FieldWritten(thread,{},method,0,field,value);}
}
#endif
} // namespace artdcevm::final_write_guard

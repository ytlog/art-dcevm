#include "platform/common/runtime_heap_receiver.h"
#include "platform/abi/common/jni_current_thread.h"
#include "platform/abi/common/suspend_all.h"
#include <mutex>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
namespace artdcevm::runtime_heap_receiver {
namespace {
// heap.h/gc_cause.h/ti_heap.cc, ART 88baac34da072b41917b4adc1987c55546266b33:
// ForceGarbageCollection calls CollectGarbage(false, kGcCauseExplicit).
constexpr char kCollect[]="_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE";
constexpr int32_t kExplicit=3;
using Collect=void(*)(void*,bool,int32_t);
void*original_storage=nullptr;abi::common::CurrentThread current=nullptr;
std::mutex installation;bool installed=false,attempted=false;
struct Observation {void*self=nullptr,*heap=nullptr;size_t calls=0;bool conflict=false;};
thread_local Observation*active=nullptr;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
void Observe(void*heap,bool clear,int32_t cause){
 if(active){
  ++active->calls;
  if(!heap||clear||cause!=kExplicit||current()!=active->self||
     (active->heap&&active->heap!=heap))active->conflict=true;
  else active->heap=heap;
 }
 reinterpret_cast<Collect>(original_storage)(heap,clear,cause);
}
bool Install(HookEngine&hooks,std::string*error){
 std::lock_guard<std::mutex>guard(installation);
 if(installed)return true;
 if(attempted)return Fail(error,"Heap receiver observation installation failed; restart process");
 if(!hooks.Initialize(error))return false;
 void*target=hooks.FindSymbol("libart.so",kCollect);
 auto current_leaf=reinterpret_cast<abi::common::CurrentThread>(hooks.FindSymbol("libart.so",abi::common::kCurrentThread));
 auto construct=reinterpret_cast<abi::common::SuspendConstruct>(hooks.FindSymbol("libart.so",abi::common::kSuspendConstruct));
 auto destroy=reinterpret_cast<abi::common::SuspendDestroy>(hooks.FindSymbol("libart.so",abi::common::kSuspendDestroy));
 if(!target||!current_leaf||!construct||!destroy)return Fail(error,"Named Heap receiver observation dependencies unavailable");
 abi::common::SuspendAll storage{};construct(&storage,"artdcevm Heap receiver observation",false);
 struct Resume{abi::common::SuspendDestroy destroy;abi::common::SuspendAll&storage;~Resume(){destroy(&storage);}}resume{destroy,storage};
 attempted=true;current=current_leaf;
 // Publish the original through the hook engine's output before the proxy can
 // run, as with the existing listener bridge. Ordinary foreign calls remain
 // passthrough even while no calibration episode is active.
 void*token=hooks.Install(target,reinterpret_cast<void*>(Observe),&original_storage,error);
 if(token&&!original_storage)std::abort();
 if(!token&&original_storage)std::abort();
 installed=token&&original_storage;
 return installed||Fail(error,"Heap receiver observation hook unavailable; restart process");
}
}
bool Calibrate(HookEngine&hooks,JNIEnv*env,JavaVM*vm,jvmtiEnv*owned,void**out,std::string*error){
 if(!env||!vm||!owned||!out||active||env->ExceptionCheck())return Fail(error,"Heap receiver observation caller invalid");
 JavaVM*actual=nullptr;JNIEnv*attached=nullptr;
 if(env->GetJavaVM(&actual)!=JNI_OK||actual!=vm||
    vm->GetEnv(reinterpret_cast<void**>(&attached),JNI_VERSION_1_6)!=JNI_OK||attached!=env||env->ExceptionCheck())
  return Fail(error,"Heap receiver observation JNI VM owner differs");
 if(!Install(hooks,error))return false;
 Observation observation;observation.self=current();
 if(!observation.self)return Fail(error,"Heap receiver observation current Thread unavailable");
 struct Borrow{~Borrow(){active=nullptr;}}borrow;active=&observation;
 const auto result=owned->ForceGarbageCollection();
 active=nullptr;
 if(result!=JVMTI_ERROR_NONE||env->ExceptionCheck()||current()!=observation.self||
    observation.conflict||observation.calls!=1||!observation.heap)
  return Fail(error,"Public ForceGC did not establish a unique original Heap receiver");
 *out=observation.heap;return true;
}
}

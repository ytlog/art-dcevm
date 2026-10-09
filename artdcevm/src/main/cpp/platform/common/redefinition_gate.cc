#include "platform/common/redefinition_gate.h"
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstring>
#if defined(__ANDROID__)
#include <sys/uio.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#include "platform/common/method_identity_calibration.h"
namespace artdcevm::redefinition_gate {
namespace {
// Fixed AOSP11 34f62af / 12--17: static RedefineClassesDirect, six
// arguments. Platform vector/string are borrowed, never NDK STL proxies.
constexpr char symbol[]="_ZN12openjdkjvmti9Redefiner21RedefineClassesDirectEPNS_11ArtJvmTiEnvEPN3art7RuntimeEPNS3_6ThreadERKNSt3__16vectorINS_18ArtClassDefinitionENS8_9allocatorISA_EEEENS_16RedefinitionTypeEPNS8_12basic_stringIcNS8_11char_traitsIcEENSB_IcEEEE";
using Direct=jvmtiError(*)(void*,void*,void*,const void*,int32_t,void*);
std::atomic<Direct> original{nullptr};JavaVM* startup_vm=nullptr;
std::atomic<bool> ready{false};std::mutex install_mutex,operation;
thread_local bool owned=false;thread_local bool direct=false;
std::atomic<const char*> diagnostic{"Startup redefinition gate was not installed before LIVE"};
jvmtiError Proxy(void* env,void* runtime,void* self,const void* definitions,int32_t type,void* error){
 if(direct)return JVMTI_ERROR_NOT_AVAILABLE;
 const bool borrowed=owned;
 if(!borrowed&&!operation.try_lock())return JVMTI_ERROR_NOT_AVAILABLE;
 struct Release {bool borrowed;~Release(){direct=false;if(!borrowed)operation.unlock();}} release{borrowed};
 direct=true;auto body=original.load(std::memory_order_acquire);return body?body(env,runtime,self,definitions,type,error):JVMTI_ERROR_NOT_AVAILABLE;
}
}
bool InstallStartup(HookEngine& hooks,JavaVM* vm,jvmtiEnv* ti,std::string* error){
 std::lock_guard<std::mutex> guard(install_mutex);
 if(ready.load(std::memory_order_acquire))return vm==startup_vm;
 jvmtiPhase phase{};
 if(!vm||!ti||ti->GetPhase(&phase)!=JVMTI_ERROR_NONE||phase!=JVMTI_PHASE_ONLOAD){
  diagnostic="Atomic batch requires real Agent_OnLoad; LIVE attach has unproven preexisting redefinitions";
  if(error)*error=diagnostic.load();return false;
 }
 if(!original){
  void* target=hooks.FindSymbol("libopenjdkjvmti.so",symbol);void* saved=nullptr;
  if(!target||!hooks.Initialize(error))return false;
  void* token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&saved,error);
  if(!token)return false;
  if(!saved)std::abort();original=reinterpret_cast<Direct>(saved);
 }
 if(ti->GetPhase(&phase)!=JVMTI_ERROR_NONE||phase!=JVMTI_PHASE_ONLOAD){
  // Keep the transparent gate: a new LIVE caller may already hold its trampoline.
  diagnostic="VM left ONLOAD during gate installation";if(error)*error=diagnostic.load();return false;
 }
 startup_vm=vm;diagnostic="Startup gate serializes all normal Direct redefine/retransform entries";
 ready.store(true,std::memory_order_release);return true;
}
namespace {
// Read the named lifetime witness through the OS self-memory interface. ART
// writes an ordinary pointer; a racing C++/atomic dereference would not prove
// absence of a C++ data race. Only a complete naturally-aligned pointer read
// is accepted. This is one named ELF object, never an address/offset search.
bool ReadLifetimeWitness(const void* source,void** result){
 if(reinterpret_cast<uintptr_t>(source)%alignof(void*)!=0)return false;
#if defined(__ANDROID__)
 iovec local{result,sizeof(*result)},remote{const_cast<void*>(source),sizeof(*result)};
 return process_vm_readv(getpid(),&local,1,&remote,1,0)==ssize_t(sizeof(*result));
#elif defined(__APPLE__)
 mach_vm_size_t copied=0;
 return mach_vm_read_overwrite(mach_task_self(),reinterpret_cast<mach_vm_address_t>(source),sizeof(*result),reinterpret_cast<mach_vm_address_t>(result),&copied)==KERN_SUCCESS&&copied==sizeof(*result);
#else
 (void)source;(void)result;return false;
#endif
}
}
bool InstallFirstLive(HookEngine& hooks,JavaVM* vm,std::string* error){
 std::lock_guard<std::mutex> guard(install_mutex);
 if(Ready(vm))return true;
 const auto fail=[&](const char* why){diagnostic=why;if(error)*error=why;return false;};
 if(!vm)return fail("Missing JavaVM for pristine plugin proof");
 if(!hooks.Initialize(error))return false;
 auto* handler=static_cast<void**>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti13gEventHandlerE"));
 size_t extent=0;
 if(!handler||!method_identity::ReadableMappingExtent(handler,&extent)||extent<sizeof(void*))
  return fail("Named plugin lifetime witness is absent or unreadable");
 if(!original){
  void* target=hooks.FindSymbol("libopenjdkjvmti.so",symbol);void* saved=nullptr;
  if(!target)return fail("Named Direct entry is absent");
  void* token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&saved,error);
  if(!token)return false;
  if(!saved)std::abort();original=reinterpret_cast<Direct>(saved);
 }
 // Fixed OpenjdkJvmTi.cc: zero-initialized gEventHandler is assigned by
 // ArtPlugin_Initialize before adding GetEnvHandler. Deinitialize neither
 // deletes nor resets it. Read AFTER the gate is published: null excludes all
 // historical environments, including disposed ones. Concurrent Initialize
 // may finish later; every subsequent Direct already traverses this gate.
 // This named lifetime witness is not a registry-empty snapshot.
 void* witness=nullptr;
 if(!ReadLifetimeWitness(handler,&witness))return fail("Kernel self-memory lifetime witness unavailable");
 if(witness!=nullptr)
  return fail("JVMTI plugin already initialized; historical Direct quiescence is unproven");
 startup_vm=vm;diagnostic="Pristine plugin lifetime proven after Direct gate installation";
 ready.store(true,std::memory_order_release);return true;
}
bool Ready(JavaVM* vm){return ready.load(std::memory_order_acquire)&&vm==startup_vm;}
const char* Diagnostic(){return diagnostic.load();}
Lease::Lease(JavaVM* vm){if(Ready(vm)&&!owned&&!direct&&operation.try_lock()){owned=true;acquired_=true;}}
Lease::~Lease(){if(acquired_){if(direct)std::abort();owned=false;operation.unlock();}}
}

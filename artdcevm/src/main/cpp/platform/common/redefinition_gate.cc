#include "platform/common/redefinition_gate.h"
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#if defined(__ANDROID__)
#include <sys/uio.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/common/suspend_all.h"
namespace artdcevm::redefinition_gate {
namespace {
// Fixed AOSP11 34f62af / 12--17: static RedefineClassesDirect, six
// arguments. Platform vector/string are borrowed, never NDK STL proxies.
constexpr char symbol[]="_ZN12openjdkjvmti9Redefiner21RedefineClassesDirectEPNS_11ArtJvmTiEnvEPN3art7RuntimeEPNS3_6ThreadERKNSt3__16vectorINS_18ArtClassDefinitionENS8_9allocatorISA_EEEENS_16RedefinitionTypeEPNS8_12basic_stringIcNS8_11char_traitsIcEENSB_IcEEEE";
using Direct=jvmtiError(*)(void*,void*,void*,const void*,int32_t,void*);
std::atomic<Direct> original{nullptr};JavaVM* startup_vm=nullptr;
std::atomic<DirectPreparation>preparation{nullptr};
std::atomic<const DirectObservation*>observation{nullptr};
std::atomic<bool> ready{false};std::mutex install_mutex,operation;
thread_local bool owned=false;thread_local bool direct=false;
std::atomic<const char*> diagnostic{"Startup redefinition gate was not installed before LIVE"};
jvmtiError Proxy(void* env,void* runtime,void* self,const void* definitions,int32_t type,void* error){
 // ART reports overlapping redefinition contention as INTERNAL. This
 // transparent public entry must not manufacture a capability denial (98).
 // Never wait here: an original ClassFileLoadHook can reenter this entry.
 if(direct)return JVMTI_ERROR_INTERNAL;
 const bool borrowed=owned;
 if(!borrowed&&!operation.try_lock())return JVMTI_ERROR_INTERNAL;
 struct Release {bool borrowed;~Release(){direct=false;if(!borrowed)operation.unlock();}} release{borrowed};
 direct=true;
 if(auto prepare=preparation.load(std::memory_order_acquire)){
  const auto result=prepare(env,runtime,self,definitions,type);
  if(result!=JVMTI_ERROR_NONE)return result;
 }
 // This scope ends before Release unlocks operation or resets Direct. No
 // concurrent public/private batch can replace the provenance being recorded.
 struct Observe {
  const DirectObservation*observer;void*token=nullptr;
  jvmtiError result=JVMTI_ERROR_INTERNAL;bool returned=false;
  ~Observe(){if(observer)observer->end(observer->owner,token,result,returned);}
 }observed{borrowed?nullptr:observation.load(std::memory_order_acquire)};
 if(observed.observer)observed.token=observed.observer->begin(observed.observer->owner,env,runtime,self,definitions,type);
 auto body=original.load(std::memory_order_acquire);
 observed.result=body?body(env,runtime,self,definitions,type,error):JVMTI_ERROR_NOT_AVAILABLE;
 observed.returned=true;return observed.result;
}
bool EnsureDirect(HookEngine&hooks,std::string*error,void*known_target=nullptr){
 if(original.load(std::memory_order_acquire))return true;
 if(!known_target&&!hooks.Initialize(error))return false;
 void*target=known_target?known_target:hooks.FindSymbol("libopenjdkjvmti.so",symbol);void*saved=nullptr;
 if(!target){if(error)*error="Named Direct entry is absent";return false;}
 void*token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&saved,error);
 if(!token){if(saved)std::abort();return false;}
 if(!saved||saved==target)std::abort();
 original.store(reinterpret_cast<Direct>(saved),std::memory_order_release);return true;
}

bool EnsureInstalledDirect(HookEngine&hooks,std::string*error){
 if(!original.load(std::memory_order_acquire)){
  // Acquire installation serialization and resolve every named dependency
  // BEFORE stopping threads. Never acquire install_mutex inside another
  // installer's stop: a suspended installer could already hold that mutex.
  if(!hooks.Initialize(error))return false;
  using namespace ::artdcevm::abi::common;
  void*target=hooks.FindSymbol("libopenjdkjvmti.so",symbol);
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!target||!construct||!destroy){if(error)*error="Named Direct installation stop unavailable";return false;}
  SuspendAll stop{};construct(&stop,"artdcevm source Direct preparation",false);
  struct Resume{SuspendAll&stop;SuspendDestroy destroy;~Resume(){destroy(&stop);}}resume{stop,destroy};
  if(!EnsureDirect(hooks,error,target))return false;
 }
 return true;
}
}
bool InstallDirectPreparation(HookEngine&hooks,DirectPreparation prepare,std::string*error){
 std::lock_guard<std::mutex>guard(install_mutex);
 const auto previous=preparation.load(std::memory_order_acquire);
 if(!prepare||(previous&&previous!=prepare)){
  if(error)*error="Named Direct already has a different preparation owner";return false;
 }
 if(!EnsureInstalledDirect(hooks,error))return false;
 preparation.store(prepare,std::memory_order_release);return true;
}
bool InstallDirectObservation(HookEngine&hooks,const DirectObservation*observer,std::string*error){
 std::lock_guard<std::mutex>guard(install_mutex);
 const auto previous=observation.load(std::memory_order_acquire);
 if(!observer||!observer->owner||!observer->begin||!observer->end||(previous&&previous!=observer)){
  if(error)*error="Named Direct observation owner is incomplete or changed";return false;
 }
 if(!EnsureInstalledDirect(hooks,error))return false;
 observation.store(observer,std::memory_order_release);return true;
}
void* MapPluginForStartup(HookEngine& hooks,JavaVM* vm,std::string* report){
 constexpr const char* plugin="libopenjdkjvmti.so";
 // ART Plugin::Load calls dlopen in libart's linker namespace, then separately
 // dlsym/executes ArtPlugin_Initialize. Merely mapping does neither operation.
 void* image=dlopen(plugin,RTLD_LAZY|RTLD_LOCAL);
 if(image){if(report)*report="plain dlopen mapped plugin; initializer not called";return image;}
 const char* failure=dlerror();std::string plain=failure?failure:"plain dlopen returned null without dlerror";
 if(!vm||!hooks.Initialize(report)){if(report)*report="plain dlopen failed: "+plain+"; native loader unavailable";return nullptr;}
 // Bionic's fixed public linker leaf receives an explicit caller address and
 // selects the owning library's namespace. This is a named libart code symbol,
 // never a manufactured return address, namespace pointer or field offset.
 using Created=jint (*)(JavaVM**,jsize,jsize*);
 void* carrier=hooks.FindSymbol("libart.so","JNI_GetCreatedJavaVMs");
 if(!carrier)carrier=hooks.FindSymbol("libartd.so","JNI_GetCreatedJavaVMs");
 Dl_info owner{};JavaVM* observed=nullptr;jsize count=0;
 const auto basename=[](const char* name){if(!name)return "";const auto* slash=std::strrchr(name,'/');return slash?slash+1:name;};
 if(!carrier||!dladdr(carrier,&owner)||(std::strcmp(basename(owner.dli_fname),"libart.so")&&std::strcmp(basename(owner.dli_fname),"libartd.so"))||
    reinterpret_cast<Created>(carrier)(&observed,1,&count)!=JNI_OK||count!=1||observed!=vm){
  if(report)*report="plain dlopen failed: "+plain+"; named ART namespace carrier failed actual public VM proof";return nullptr;
 }
 using Open=void* (*)(const char*,int,const void*);
 void* named=dlsym(RTLD_DEFAULT,"__loader_dlopen");
 if(!named)named=hooks.FindSymbol("linker64","__loader_dlopen");
 if(!named)named=hooks.FindSymbol("linker","__loader_dlopen");
 if(!named){if(report)*report="plain dlopen failed: "+plain+"; fixed named bionic namespace loader unavailable";return nullptr;}
 image=reinterpret_cast<Open>(named)(plugin,RTLD_LAZY|RTLD_LOCAL,carrier);
 if(image){if(report)*report="plain dlopen failed: "+plain+"; named loader mapped plugin in independently observed ART VM namespace; initializer not called";return image;}
 const char* native_failure=dlerror();
 if(report)*report="plain dlopen failed: "+plain+"; named ART namespace mapping failed: "+(native_failure?native_failure:"no dlerror");return nullptr;
}
bool InstallStartup(HookEngine& hooks,JavaVM* vm,jvmtiEnv* ti,std::string* error){
 std::lock_guard<std::mutex> guard(install_mutex);
 if(ready.load(std::memory_order_acquire))return vm==startup_vm;
 jvmtiPhase phase{};
 if(!vm||!ti||ti->GetPhase(&phase)!=JVMTI_ERROR_NONE||phase!=JVMTI_PHASE_ONLOAD){
  diagnostic="Atomic batch requires real Agent_OnLoad; LIVE attach has unproven preexisting redefinitions";
  if(error)*error=diagnostic.load();return false;
 }
 if(!EnsureDirect(hooks,error))return false;
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
 if(!EnsureDirect(hooks,error))return false;
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
bool Owned(JavaVM* vm){return Ready(vm)&&owned;}
const char* Diagnostic(){return diagnostic.load();}
Lease::Lease(JavaVM* vm){if(Ready(vm)&&!owned&&!direct&&operation.try_lock()){owned=true;acquired_=true;}}
Lease::~Lease(){if(acquired_){if(direct)std::abort();owned=false;operation.unlock();}}
}

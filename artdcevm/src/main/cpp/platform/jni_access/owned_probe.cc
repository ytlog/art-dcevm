// Debug-only owned JNI calibration, independent of the full evolution backend.
#ifdef ARTDCEVM_TESTING
#include "platform/jni_access/capability_selection.h"
#include "platform/android15/runtime_services.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/modern_method_guards.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/runnable_global_ref_bridge.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/owned_context_calibration.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/android17/method_table.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/native_method_binding_calibration.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/owned_method_table.h"
#include "platform/android17/runtime_services.h"
#include "platform/common/owned_jit_receivers.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/arm64/android15/jni_current_thread.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include "hooks/shadow_hook_engine.h"
#include "support/elf_modules.h"
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace artdcevm::jni_access {
namespace {

owned_context_calibration::Service context_service;
RuntimeCandidate profile{};

bool initialized=false;
thread_local const char* last_route="uncalibrated";
thread_local int last_table_count=-1;
thread_local size_t last_cache_threads=0;
thread_local method_identity::Calibration last_method_model;
thread_local runtime_services::Snapshot runtime_services_observed;
std::mutex initialization;
abi::arm64::android11::CurrentThread current=nullptr;
abi::arm64::android11::CurrentPeer const_peer=nullptr;
abi::arm64::android15::CurrentPeer mutable_peer=nullptr;
abi::arm64::android11::DecodeJObject legacy_decode=nullptr;
runnable_jni::GlobalDecoder global_decode;
struct Probe {JNIEnv* env;JavaVM* vm;void* self;jobject carrier;jobject thread_global;int hits=0;bool use_global=false;};
bool Observe(const runnable_jni::Context& context,void* data) {
  auto& probe=*static_cast<Probe*>(data);
  if(context.env!=probe.env||context.vm!=probe.vm||context.self!=probe.self||current()!=context.self)return false;
  if(context.env->GetObjectRefType(probe.thread_global)!=JNIGlobalRefType||context.env->ExceptionCheck())return false;
  void* object=nullptr;
  if(profile.decoder==DecoderModel::kLegacyAny)
    object=reinterpret_cast<void*>(legacy_decode(context.self,probe.thread_global).reference);
  else if(!global_decode.Decode(context,probe.thread_global,&object))return false;
  // No JNI safepoint or managed dereference between the independently obtained
  // borrowed identities. Never take this current-thread oracle to another Thread.
  void* peer=const_peer?const_peer(context.self):mutable_peer(context.self);
  if(!object||object!=peer)return false;
  auto nested=runnable_executor::With(context.env,probe.thread_global,Observe,data);
  if(nested!=runnable_jni::Result::kReentrant)return false;
  ++probe.hits;return true;
}
struct MethodProbe {
  Probe probe;
  abi::arm64::android_common::FromReflectedMethod leaf;
  const method_identity::Oracle* oracles;
  size_t count;
  method_identity::Calibration calibration;
  jobject class_global;
  evolution::MappedDexMemberCounts counts;
  bool table_candidate;
  const void* registered_binding;
};
void JNICALL OwnedNativeMarker(JNIEnv*, jobject) {}
bool UnretiredOwnedFixture(void*) { return false; }
bool ObserveMethods(const runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<MethodProbe*>(data);
  if(!Observe(context,&request.probe))return false;
  std::string error;
  if(!method_identity::Calibrate(context,request.leaf,request.oracles,request.count,
                                    &request.calibration,&error))return false;
  last_method_model=request.calibration;
  void* klass=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,request.class_global,&klass))return false;
  method_array::View table;
  if(!owned_method_table::Calibrate(context,request.leaf,request.calibration,klass,
      request.counts,request.oracles,request.count,&table,&error))return false;
  if(request.calibration.model==method_identity::Model::kMethod12Onward) {
  if(!executable_mirror::Calibrate(context,request.leaf,
      runnable_executor::DecodeStrongGlobal,request.oracles,request.count,&error))return false;
  bool native_checked=false;
  for(size_t i=0;i<request.count;++i)if(request.oracles[i].java_modifiers&0x100u) {
    if(!native_method_binding::Calibrate(context,request.leaf,request.oracles[i],
        request.registered_binding,&error))return false;
    native_checked=true;
  }
  if(!native_checked)return false;
  if(runtime_services_observed.receivers.class_linker) {
    ShadowHookEngine hooks;
    size_t native_index=0;
    while(native_index<request.count&&!(request.oracles[native_index].java_modifiers&0x100u))++native_index;
    auto service=modern_methods::Service::Prepare(hooks,context,request.leaf,
        request.oracles[native_index],request.registered_binding,
        runtime_services_observed.receivers.class_linker,klass,table,klass,table,
        UnretiredOwnedFixture,&error);
    if(!service||service->Count(klass)!=table.count)return false;
    for(size_t i=0;i<table.count;++i) {
      void* method=service->At(klass,i);
      if(service->DeclaringClass(method)!=klass||!service->SameIdentity(method,method)||
          service->IsRetired(method)||service->Flags(method)!=method_metadata::ReadAccessFlags(method))return false;
    }
    // Named name/signature comparison must distinguish two independently
    // declared methods, including the constructor.
    if(table.count>1&&service->SameIdentity(service->At(klass,0),service->At(klass,1)))return false;
  }
  }
  last_table_count=static_cast<int>(table.boundaries.declared_count);
  return true;
}
int Run(JNIEnv* env,jobject carrier,jobject thread_global) {
  last_route="uncalibrated";
  if(!env||!carrier||!thread_global||env->ExceptionCheck())return -1;
  JavaVM* vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -2;
  std::unique_lock<std::mutex> guard(initialization);
  if(!initialized) {
    RuntimeCandidate candidate{};std::string error;
    static ShadowHookEngine hooks;if(!hooks.Initialize(&error))return -4;
    auto resolve=[](void* context,const char* name)->void* {
      return static_cast<ShadowHookEngine*>(context)->FindSymbol("libart.so",name);
    };
    if(!SelectRuntimeCandidate(resolve,&hooks,&candidate,&error))return -3;
    auto get_current=reinterpret_cast<decltype(current)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    const char* peer_name=candidate.peer==PeerModel::kConst?abi::arm64::android11::kCurrentPeer:abi::arm64::android15::kCurrentPeer;
    const char* decode_name=candidate.decoder==DecoderModel::kLegacyAny?abi::arm64::android11::kDecodeJObject:runnable_jni::GlobalDecoder::SymbolName();
    void* peer=hooks.FindSymbol("libart.so",peer_name);
    void* decode=hooks.FindSymbol("libart.so",decode_name);
    if(!get_current||!peer||!decode)return -5;
    if(!runnable_executor::Initialize(hooks,env,&error))return -6;
    current=get_current;profile=candidate;
    if(candidate.peer==PeerModel::kConst)const_peer=reinterpret_cast<decltype(const_peer)>(peer);
    else mutable_peer=reinterpret_cast<decltype(mutable_peer)>(peer);
    if(candidate.decoder==DecoderModel::kLegacyAny)legacy_decode=reinterpret_cast<decltype(legacy_decode)>(decode);
    else if(!runnable_jni::GlobalDecoder::Bind(runnable_jni::GlobalDecoder::Representation::kMatchedReleaseNoPoison,decode,&global_decode))std::abort();
    owned_context_calibration::Service::Bind(
        hooks.FindSymbol("libart.so",owned_context_calibration::Service::SymbolName()),&context_service);
    initialized=true;
    // Binding is a diagnostic candidate only. Observed release pointer equality
    // below does not mint any production calibration token or evolution profile.
  }
  guard.unlock();
  void* self=current();if(!self||env->GetObjectRefType(thread_global)!=JNIGlobalRefType)return -7;
  jmethodID expected=env->FromReflectedMethod(carrier);if(!expected||env->ExceptionCheck())return -8;
  Probe probe{env,vm,self,carrier,thread_global};
  for(int i=0;i<128;++i) {
    auto result=runnable_executor::With(env,thread_global,Observe,&probe);
    if(result!=runnable_jni::Result::kSuccess)return -100-static_cast<int>(result);
    if(current()!=self||env->FromReflectedMethod(carrier)!=expected||env->ExceptionCheck())return -10;
  }
  if(probe.hits!=128)return -11;
  last_route=runnable_executor::Route();
  return 128;
}
}
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probe(JNIEnv* env,jclass,jobject carrier,jobject thread) {
  // Own the JNI root on the calling native thread, before any scope calibration.
  if(!thread||env->ExceptionCheck())return -1;
  jobject global=env->NewGlobalRef(thread);if(!global)return -12;
  int result=artdcevm::jni_access::Run(env,carrier,global);
  env->DeleteGlobalRef(global);return result;
}
#endif
#if defined(ARTDCEVM_TESTING)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_methodTableCount(JNIEnv*,jclass) {
  return artdcevm::jni_access::last_table_count;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeJitServices(JNIEnv* env,jclass,jobject hot,jclass owner,jint index,jint flags,jclass target,jbyteArray dex) {
  using namespace artdcevm;
  if(!hot||!owner||!target||!dex||env->ExceptionCheck())return nullptr;
  struct Roots {JNIEnv* env;jobject method,klass;
    ~Roots(){if(method)env->DeleteGlobalRef(method);if(klass)env->DeleteGlobalRef(klass);}
  } roots{env,env->NewGlobalRef(hot),nullptr};
  if(!roots.method||env->ExceptionCheck())return nullptr;
  roots.klass=env->NewGlobalRef(owner);
  if(!roots.klass||env->ExceptionCheck()||env->GetArrayLength(dex)<=0)return nullptr;
  jobject method=roots.method,klass=roots.klass;
  std::vector<unsigned char> bytes(static_cast<size_t>(env->GetArrayLength(dex)));
  env->GetByteArrayRegion(dex,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));
  ShadowHookEngine hooks;std::string error;runtime_services::View view;
  method_identity::Oracle oracle{method,static_cast<uint32_t>(index),static_cast<uint32_t>(flags)};
  bool passed=owned_jit_receivers::CalibrateJitReceivers(hooks,env,klass,oracle,
      jni_access::last_method_model,jni_access::runtime_services_observed,target,bytes.data(),bytes.size(),&view,&error);
  if(env->ExceptionCheck())return nullptr;
  return env->NewStringUTF(passed?"jit,code-cache,heap":error.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_artBuildId(JNIEnv* env,jclass) {
  auto module=artdcevm::FindLoadedModule("libart.so");
  return env->NewStringUTF(module.build_id.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeRoute(JNIEnv* env,jclass) {
  return env->NewStringUTF(artdcevm::jni_access::last_route);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeContext(JNIEnv*,jclass) {
  artdcevm::owned_context_calibration::Service service;
  {
    std::lock_guard<std::mutex> lock(artdcevm::jni_access::initialization);
    service=artdcevm::jni_access::context_service;
  }
  artdcevm::owned_context_calibration::Report report;
  if(!service.Run(128,&report))return -200-static_cast<int>(report.failure);
  return report.completed==128&&report.checks==384?384:-201;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeMethods(
    JNIEnv* env,jclass,jobjectArray methods,jintArray indices,jintArray modifiers,
    jclass owner,jobject carrier,jobject thread,jint direct,jint virtual_count,jint table_candidate) {
  using namespace artdcevm;
  using namespace artdcevm::jni_access;
  if(!methods||!indices||!modifiers||!owner||!carrier||!thread||env->ExceptionCheck())return -300;
  const jsize count=env->GetArrayLength(methods);
  if(count<3||count>32||env->GetArrayLength(indices)!=count||env->GetArrayLength(modifiers)!=count)return -301;
  std::vector<jint> dex(count),flags(count);
  env->GetIntArrayRegion(indices,0,count,dex.data());
  env->GetIntArrayRegion(modifiers,0,count,flags.data());
  if(env->ExceptionCheck())return -302;
  struct Roots {
    JNIEnv* env;std::vector<jobject> values;
    ~Roots(){for(jobject root:values)env->DeleteGlobalRef(root);}
    jobject Own(jobject value){auto root=env->NewGlobalRef(value);if(root)values.push_back(root);return root;}
  } roots{env,{}};
  jobject class_global=roots.Own(owner);
  if(!class_global)return -303;
  last_table_count=-1;
  jobject thread_global=roots.Own(thread);
  if(!thread_global)return -303;
  std::vector<method_identity::Oracle> oracles;
  for(jsize i=0;i<count;++i) {
    jobject method=env->GetObjectArrayElement(methods,i);
    if(!method||env->ExceptionCheck())return -304;
    jobject global=roots.Own(method);env->DeleteLocalRef(method);
    if(!global||env->ExceptionCheck())return -303;
    oracles.push_back({global,static_cast<uint32_t>(dex[i]),static_cast<uint32_t>(flags[i])});
  }
  JavaVM* vm=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -305;
  std::unique_lock<std::mutex> guard(initialization);
  if(!initialized||!std::strcmp(last_route,"uncalibrated"))return -306;
  ShadowHookEngine hooks;
  auto leaf=reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(
      hooks.FindSymbol("libart.so",runnable_jni::Bridge::SymbolName()));
  if(!leaf)return -307;
  // This debug-owned class has exactly one private native fixture. Public JNI
  // registration supplies the independent mutable-data oracle; every worker
  // registers the same binding, so no competing binding changes are possible.
  if(table_candidate) {
    JNINativeMethod registration{const_cast<char*>("nativeMarker"),
        const_cast<char*>("()V"),reinterpret_cast<void*>(OwnedNativeMarker)};
    if(env->RegisterNatives(owner,&registration,1)!=JNI_OK||env->ExceptionCheck())return -309;
  }
  Probe probe{env,vm,current(),carrier,thread_global,0,!std::strcmp(last_route,"new-global-ref")};
  MethodProbe request{probe,leaf,oracles.data(),oracles.size(),{},class_global,
      {0,0,static_cast<uint32_t>(direct),static_cast<uint32_t>(virtual_count)},table_candidate!=0,
      reinterpret_cast<void*>(OwnedNativeMarker)};
  guard.unlock();
  auto result=runnable_executor::With(env,thread_global,ObserveMethods,&request);
  if(result!=runnable_jni::Result::kSuccess)return -300-static_cast<int>(result);
  if(request.probe.hits!=1||request.calibration.identities.size()!=oracles.size())return -308;
  return request.calibration.model==method_identity::Model::kMethod11?11:12;
}
#endif

#if defined(ARTDCEVM_TESTING)
namespace artdcevm::jni_access {
bool CalibrateServices(HookEngine& hooks,JNIEnv* env,std::string* error) {
  runtime_services_observed={};
  // Named source declaration chooses only the observer ABI; actual receivers
  // must be reached and independently match Runtime before publication.
  if(hooks.FindSymbol("libart.so","_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjNS1_12ListenerTypeE")) {
    android17::RuntimeServices services;
    if(!android17::CalibrateRuntimeServices(hooks,env,&services,error))return false;
    runtime_services_observed=services;
  } else {
    android15::RuntimeServices services;
    if(!android15::CalibrateRuntimeServices(hooks,env,&services,error))return false;
    runtime_services_observed={services.runtime,services.instrumentation,services.receivers};
  }
  return true;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeRuntimeServices(JNIEnv* env,jclass) {
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(!artdcevm::jni_access::CalibrateServices(hooks,env,&error)&&error.empty())
    error="Runtime service calibration failed without diagnostic";
  return env->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeNativeCache(JNIEnv* env,jclass) {
  artdcevm::jni_access::last_cache_threads=0;
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(artdcevm::jni_access::CalibrateServices(hooks,env,&error)) {
    const auto& services=artdcevm::jni_access::runtime_services_observed;
    artdcevm::native_cache::Service cache;
    if(cache.Bind(hooks,services.receivers.thread_list,&error)&&cache.Prepare(&error)) {
      using namespace artdcevm::abi::arm64::android_common;
      auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
      auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
      if(!construct||!destroy)error="Missing named owned cache calibration stop";
      else {SuspendAll storage{};construct(&storage,"artdcevm synchronous native cache clearing",false);
        size_t count=0;cache.ClearStoppedWorld(&count,&error);destroy(&storage);
        artdcevm::jni_access::last_cache_threads=count;}
    }
  }
  if(error.empty()&&!artdcevm::jni_access::runtime_services_observed.runtime)error="Runtime service calibration failed without diagnostic";
  return env->NewStringUTF(error.c_str());
}

#endif

#if defined(ARTDCEVM_TESTING)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeCacheThreads(JNIEnv*,jclass) {
  return static_cast<jint>(artdcevm::jni_access::last_cache_threads);
}
#endif

#if defined(ARTDCEVM_TESTING)
namespace {
artdcevm::retirement::RetiredMethodRegistry debug_retired_entries;
struct RetireEntryRequest {
  void* manager; jmethodID id;
  void* (*decode)(void*,jmethodID);
};
bool RetireEntry(const artdcevm::runnable_jni::Context&,void* data) {
  auto& request=*static_cast<RetireEntryRequest*>(data);
  auto* method=request.decode(request.manager,request.id);
  if(!method)return false;
  debug_retired_entries.Retire(method);
  return debug_retired_entries.Contains(method);
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeMethodGuards(JNIEnv* env,jclass,jclass owner) {
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(!artdcevm::jni_access::CalibrateServices(hooks,env,&error))
    return env->NewStringUTF(error.c_str());
  const auto& services=artdcevm::jni_access::runtime_services_observed;
  auto root=env->NewGlobalRef(owner);
  if(!root)return env->NewStringUTF("Missing owned guard fixture root");
  auto removed=env->GetStaticMethodID(owner,"removed","()I");
  auto kept=env->GetStaticMethodID(owner,"kept","()I");
  auto exception=env->FindClass("java/lang/NoSuchMethodError");
  bool success=removed&&kept&&exception&&!env->ExceptionCheck();
  if(success&&debug_retired_entries.Count()==0&&env->CallStaticIntMethodA(owner,removed,nullptr)!=41)
    success=false;
  if(success)success=artdcevm::retirement::InstallMethodEntryGuards(hooks,env,root,
      services.receivers.jni_id_manager,debug_retired_entries,&error);
  RetireEntryRequest request{services.receivers.jni_id_manager,removed,
      reinterpret_cast<void*(*)(void*,jmethodID)>(hooks.FindSymbol("libart.so",
      "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"))};
  if(success)success=request.decode&&artdcevm::runnable_executor::With(env,root,RetireEntry,&request)==
      artdcevm::runnable_jni::Result::kSuccess;
  if(success) {
    env->CallStaticIntMethodA(owner,removed,nullptr);
    auto thrown=env->ExceptionOccurred();
    env->ExceptionClear();
    success=thrown&&env->IsInstanceOf(thrown,exception);
    if(thrown)env->DeleteLocalRef(thrown);
  }
  if(success)success=env->CallStaticIntMethodA(owner,kept,nullptr)==73&&!env->ExceptionCheck();
  env->DeleteGlobalRef(root);
  if(!success&&error.empty())error="Actual method guard identity/exception/passthrough failed";
  return env->NewStringUTF(error.c_str());
}
#endif

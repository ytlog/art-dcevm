// Debug-only owned JNI calibration, independent of the full evolution backend.
#ifdef ARTDCEVM_TESTING
#include "platform/jni_access/capability_selection.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/runnable_global_ref_bridge.h"
#include "platform/common/owned_context_calibration.h"
#include "platform/common/method_identity_calibration.h"
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
runnable_jni::Bridge bridge;
runnable_jni::GlobalRefBridge global_bridge;
owned_context_calibration::Service context_service;
RuntimeCandidate profile{};
void* token=nullptr;
void* global_token=nullptr;
bool initialized=false;
thread_local const char* last_route="uncalibrated";
std::mutex initialization;
abi::arm64::android11::CurrentThread current=nullptr;
abi::arm64::android11::CurrentPeer const_peer=nullptr;
abi::arm64::android15::CurrentPeer mutable_peer=nullptr;
abi::arm64::android11::DecodeJObject legacy_decode=nullptr;
runnable_jni::GlobalDecoder global_decode;
void* Proxy(const abi::arm64::android_common::RunnableJniScope& scope,jobject carrier) {
  return bridge.OnFromReflectedMethod(scope,carrier);
}
jobject GlobalProxy(void* vm,void* self,abi::arm64::android11::ReleaseObjectPointer object) {
  return global_bridge.OnAddGlobalRef(vm,self,object);
}
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
  auto nested=probe.use_global?global_bridge.Invoke(context.env,probe.thread_global,Observe,data)
      :bridge.Invoke(context.env,probe.carrier,Observe,data);
  if(nested!=runnable_jni::Result::kReentrant)return false;
  ++probe.hits;return true;
}
struct MethodProbe {
  Probe probe;
  abi::arm64::android_common::FromReflectedMethod leaf;
  const method_identity::Oracle* oracles;
  size_t count;
  method_identity::Calibration calibration;
};
bool ObserveMethods(const runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<MethodProbe*>(data);
  if(!Observe(context,&request.probe))return false;
  std::string error;
  return method_identity::Calibrate(context,request.leaf,request.oracles,request.count,
                                    &request.calibration,&error);
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
    auto construct=reinterpret_cast<abi::arm64::android_common::SuspendConstruct>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendConstruct));
    auto destroy=reinterpret_cast<abi::arm64::android_common::SuspendDestroy>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendDestroy));
    const char* peer_name=candidate.peer==PeerModel::kConst?abi::arm64::android11::kCurrentPeer:abi::arm64::android15::kCurrentPeer;
    const char* decode_name=candidate.decoder==DecoderModel::kLegacyAny?abi::arm64::android11::kDecodeJObject:runnable_jni::GlobalDecoder::SymbolName();
    void* peer=hooks.FindSymbol("libart.so",peer_name);
    void* decode=hooks.FindSymbol("libart.so",decode_name);
    void* target=hooks.FindSymbol("libart.so",runnable_jni::Bridge::SymbolName());
    void* global_target=hooks.FindSymbol("libart.so",runnable_jni::GlobalRefBridge::SymbolName());
    if(!get_current||!construct||!destroy||!peer||!decode||(!target&&!global_target))return -5;
    // This export is a regular JNI native method, with no Fast/CriticalNative
    // registration. ART itself enters it Native; no Thread fields are read or
    // state bits written. The fixed named scope obtains exclusive mutator access.
    abi::arm64::android_common::SuspendAll storage{};
    construct(&storage,"Owned JNI access calibration",false);
    struct Resume {abi::arm64::android_common::SuspendDestroy fn;void* storage;~Resume(){fn(storage);}} resume{destroy,&storage};
    void* original=nullptr;
    if(target) {
      token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&original,&error);
      if(token&&!runnable_jni::Bridge::Bind(original,&bridge))std::abort();
    }
    if(global_target) {
      original=nullptr;
      global_token=hooks.Install(global_target,reinterpret_cast<void*>(GlobalProxy),&original,&error);
      if(global_token&&!runnable_jni::GlobalRefBridge::Bind(original,reinterpret_cast<void*>(get_current),&global_bridge))std::abort();
    }
    if(!token&&!global_token)return -6;
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
    auto result=probe.use_global?global_bridge.Invoke(env,thread_global,Observe,&probe)
        :bridge.Invoke(env,carrier,Observe,&probe);
    // A present export may be bypassed by ART's compiled JNI caller. Select the
    // alternative only when no callback ran, never after a rejected oracle.
    if(i==0 && (result==runnable_jni::Result::kLeafNotReached||result==runnable_jni::Result::kUnavailable)) {
      probe.use_global=true;
      result=global_bridge.Invoke(env,thread_global,Observe,&probe);
    }
    if(result!=runnable_jni::Result::kSuccess)return -100-static_cast<int>(result);
    if(current()!=self||env->FromReflectedMethod(carrier)!=expected||env->ExceptionCheck())return -10;
  }
  if(probe.hits!=128)return -11;
  last_route=probe.use_global?"new-global-ref":"from-reflected-method";
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
    jclass owner,jobject carrier,jobject thread) {
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
  if(!roots.Own(owner))return -303;
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
  Probe probe{env,vm,current(),carrier,thread_global,0,!std::strcmp(last_route,"new-global-ref")};
  MethodProbe request{probe,leaf,oracles.data(),oracles.size(),{}};
  guard.unlock();
  auto result=probe.use_global?global_bridge.Invoke(env,thread_global,ObserveMethods,&request)
      :bridge.Invoke(env,carrier,ObserveMethods,&request);
  if(result!=runnable_jni::Result::kSuccess)return -300-static_cast<int>(result);
  if(request.probe.hits!=1||request.calibration.identities.size()!=oracles.size())return -308;
  return request.calibration.model==method_identity::Model::kMethod11?11:12;
}
#endif

// Debug-only owned JNI calibration, independent of the full evolution backend.
#ifdef ARTDCEVM_TESTING
#include "platform/jni_access/captured_profile.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/arm64/android15/jni_current_thread.h"
#include "platform/abi/arm64/android11/suspend_all.h"
#include "hooks/shadow_hook_engine.h"
#include <mutex>
#include <cstdlib>
namespace artdcevm::jni_access {
namespace {
runnable_jni::Bridge bridge;
CapturedProfile profile{};
void* token=nullptr;
std::mutex initialization;
abi::arm64::android11::CurrentThread current=nullptr;
abi::arm64::android11::CurrentPeer const_peer=nullptr;
abi::arm64::android15::CurrentPeer mutable_peer=nullptr;
abi::arm64::android11::DecodeJObject legacy_decode=nullptr;
runnable_jni::GlobalDecoder global_decode;
void* Proxy(const abi::arm64::android11::RunnableJniScope& scope,jobject carrier) {
  return bridge.OnFromReflectedMethod(scope,carrier);
}
struct Probe {JNIEnv* env;JavaVM* vm;void* self;jobject carrier;jobject thread_global;int hits=0;};
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
  if(bridge.Invoke(context.env,probe.carrier,Observe,data)!=runnable_jni::Result::kReentrant)return false;
  ++probe.hits;return true;
}
int Run(JNIEnv* env,jobject carrier,jobject thread_global) {
  if(!env||!carrier||!thread_global||env->ExceptionCheck())return -1;
  JavaVM* vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -2;
  std::unique_lock<std::mutex> guard(initialization);
  if(!token) {
    CapturedProfile candidate{};std::string error;
    if(!MatchCapturedProfile(FindLoadedModule("libart.so"),&candidate,&error))return -3;
    static ShadowHookEngine hooks;if(!hooks.Initialize(&error))return -4;
    auto get_current=reinterpret_cast<decltype(current)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    auto construct=reinterpret_cast<abi::arm64::android11::SuspendConstruct>(hooks.FindSymbol("libart.so",abi::arm64::android11::kSuspendConstruct));
    auto destroy=reinterpret_cast<abi::arm64::android11::SuspendDestroy>(hooks.FindSymbol("libart.so",abi::arm64::android11::kSuspendDestroy));
    const char* peer_name=candidate.peer==PeerModel::kConst?abi::arm64::android11::kCurrentPeer:abi::arm64::android15::kCurrentPeer;
    const char* decode_name=candidate.decoder==DecoderModel::kLegacyAny?abi::arm64::android11::kDecodeJObject:runnable_jni::GlobalDecoder::SymbolName();
    void* peer=hooks.FindSymbol("libart.so",peer_name);
    void* decode=hooks.FindSymbol("libart.so",decode_name);
    void* target=hooks.FindSymbol("libart.so",runnable_jni::Bridge::SymbolName());
    if(!get_current||!construct||!destroy||!peer||!decode||!target)return -5;
    // This export is a regular JNI native method, with no Fast/CriticalNative
    // registration. ART itself enters it Native; no Thread fields are read or
    // state bits written. The fixed named scope obtains exclusive mutator access.
    abi::arm64::android11::SuspendAll storage{};
    construct(&storage,"Owned JNI access calibration",false);
    struct Resume {abi::arm64::android11::SuspendDestroy fn;void* storage;~Resume(){fn(storage);}} resume{destroy,&storage};
    void* original=nullptr;token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&original,&error);
    if(!token)return -6;
    if(!runnable_jni::Bridge::Bind(original,&bridge))std::abort();
    current=get_current;profile=candidate;
    if(candidate.peer==PeerModel::kConst)const_peer=reinterpret_cast<decltype(const_peer)>(peer);
    else mutable_peer=reinterpret_cast<decltype(mutable_peer)>(peer);
    if(candidate.decoder==DecoderModel::kLegacyAny)legacy_decode=reinterpret_cast<decltype(legacy_decode)>(decode);
    else if(!runnable_jni::GlobalDecoder::Bind(runnable_jni::GlobalDecoder::Representation::kMatchedReleaseNoPoison,decode,&global_decode))std::abort();
    // Binding is a diagnostic candidate only. Observed release pointer equality
    // below does not mint any production calibration token or evolution profile.
  }
  guard.unlock();
  void* self=current();if(!self||env->GetObjectRefType(thread_global)!=JNIGlobalRefType)return -7;
  jmethodID expected=env->FromReflectedMethod(carrier);if(!expected||env->ExceptionCheck())return -8;
  Probe probe{env,vm,self,carrier,thread_global};
  for(int i=0;i<128;++i) {
    if(bridge.Invoke(env,carrier,Observe,&probe)!=runnable_jni::Result::kSuccess)return -9;
    if(current()!=self||env->FromReflectedMethod(carrier)!=expected||env->ExceptionCheck())return -10;
  }
  return probe.hits==128?128:-11;
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
#endif

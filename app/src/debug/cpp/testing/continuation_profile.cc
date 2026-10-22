// Diagnostic-only sparse timing. Never included in either production AAR.
#include "platform/common/continuation_profile.h"
#include "platform/common/private_continuation.h"
#include "platform/common/art_hook.h"
#include "platform/symbols/private_continuation_symbols.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "shadowhook.h"
#include <jni.h>
#include <time.h>
namespace artdcevm::continuation_profile {
namespace {
using Call = bool (*)(void*,void*,void*,const uint16_t*,uint16_t,uint64_t*);
Call originals[4]{};
void* hooks[4]{};
void* targets[4]{};
struct State {
 bool enabled=false, measuring=false, sampled_call=false;
 const void* last_frame=nullptr;
 const void* target_frame=nullptr;
 uint64_t event_count[2]{}, event_samples[2]{}, event_ns[2]{};
 uint64_t calls=0, call_samples=0, call_ns=0;
 uint64_t markers=0, marker_ns=0, marker_start=0, timer_ns=0;
 uint64_t retirement_count=0, retirement_samples=0, retirement_ns=0, retirement_true=0;
};
thread_local State state;
uint64_t Now() { timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec; }
template <size_t i>
bool CallHook(void* method,void* self,void* frame,const uint16_t* insn,uint16_t data,uint64_t* result) {
 if (!state.measuring || frame!=state.target_frame ||
     (static_cast<abi::arm64::ArtMethodPrefix*>(method)->access_flags.load(std::memory_order_relaxed)&0x100))
   return originals[i](method,self,frame,insn,data,result);
 const bool sample=(++state.calls%64)==0;
 state.sampled_call=sample;
 if (!sample) return originals[i](method,self,frame,insn,data,result);
 uint64_t start=Now();
 const bool ok=originals[i](method,self,frame,insn,data,result);
 state.call_ns+=Now()-start;++state.call_samples;
 return ok;
}
}
bool Pc(interpreter_events::Pc original,void* self,const void* a,const void* f,uint32_t pc,const void* i,void* r) {
 if (!state.enabled) return original(self,a,f,pc,i,r);
 state.last_frame=f;
 if (!state.measuring) return original(self,a,f,pc,i,r);
 const size_t kind=f==state.target_frame?0:1;
 if ((++state.event_count[kind]%64)!=0) return original(self,a,f,pc,i,r);
 uint64_t start=Now();bool ok=original(self,a,f,pc,i,r);
 state.event_ns[kind]+=Now()-start;++state.event_samples[kind];return ok;
}
bool RetiredLookup(const void* method, bool (*lookup)(const void*)) {
 if(!state.measuring) return lookup(method);
 const bool sample=(++state.retirement_count%67)==0;
 uint64_t start=sample?Now():0;
 bool result=lookup(method);
 if(sample) {state.retirement_ns+=Now()-start;++state.retirement_samples;}
 if(result)++state.retirement_true;
 return result;
}
void MarkerStart() { if(state.measuring&&state.sampled_call) state.marker_start=Now(); }
void MarkerEnd() { if(state.measuring&&state.marker_start) {state.marker_ns+=Now()-state.marker_start;++state.markers;state.marker_start=0;state.sampled_call=false;} }
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_configureContinuationProfile(JNIEnv* jni,jclass,jboolean prime) {
 using namespace artdcevm;
 if(!interpreter_events::Prepare())return JNI_FALSE;
 using namespace continuation_profile;
 void* replacement[]={HookAddress(CallHook<0>),HookAddress(CallHook<1>),HookAddress(CallHook<2>),HookAddress(CallHook<3>)};
 for(size_t i=0;i<4;++i) {
  targets[i]=symbols::Find(symbols::private_continuation::kCalls[i]);if(!targets[i])return JNI_FALSE;
  for(size_t j=0;j<i;++j)if(targets[i]==targets[j]) {hooks[i]=hooks[j];originals[i]=originals[j];break;}
  if(!hooks[i])hooks[i]=shadowhook_hook_sym_addr(targets[i],replacement[i],HookSlot(&originals[i]));
  if(!hooks[i])return JNI_FALSE;
 }
 JavaVM* vm=nullptr;jni->GetJavaVM(&vm);
 if(!private_continuation::Prepare(vm,jni))return JNI_FALSE;
 return !prime||private_continuation::Prove(jni);
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_fixture_ContinuationBenchmark_profileEnable(JNIEnv*,jclass) {artdcevm::continuation_profile::state.enabled=true;}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_fixture_ContinuationBenchmark_profileBegin(JNIEnv*,jclass) {
 using namespace artdcevm::continuation_profile;
 const void* frame=state.last_frame;state=State{};state.enabled=true;state.target_frame=frame;
 for(int i=0;i<4096;++i){uint64_t t=Now();state.timer_ns+=Now()-t;}state.measuring=true;
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_fixture_ContinuationBenchmark_profileEnd(JNIEnv*,jclass) {artdcevm::continuation_profile::state.measuring=false;}
extern "C" JNIEXPORT jlongArray JNICALL Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_readContinuationProfile(JNIEnv* jni,jclass) {
 using namespace artdcevm::continuation_profile;
 jlong data[]={static_cast<jlong>(state.event_count[0]),static_cast<jlong>(state.event_samples[0]),static_cast<jlong>(state.event_ns[0]),static_cast<jlong>(state.event_count[1]),static_cast<jlong>(state.event_samples[1]),static_cast<jlong>(state.event_ns[1]),static_cast<jlong>(state.calls),static_cast<jlong>(state.call_samples),static_cast<jlong>(state.call_ns),static_cast<jlong>(state.markers),static_cast<jlong>(state.marker_ns),static_cast<jlong>(state.timer_ns),static_cast<jlong>(state.retirement_count),static_cast<jlong>(state.retirement_samples),static_cast<jlong>(state.retirement_ns),static_cast<jlong>(state.retirement_true)};
 jlongArray result=jni->NewLongArray(16);if(result)jni->SetLongArrayRegion(result,0,16,data);return result;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_addEmptyProfileEnvironment(JNIEnv* jni,jclass) {
 JavaVM* vm=nullptr;jvmtiEnv* extra=nullptr;
 return jni->GetJavaVM(&vm)==JNI_OK && vm->GetEnv(reinterpret_cast<void**>(&extra),JVMTI_VERSION_1_2)==JNI_OK && extra;
}

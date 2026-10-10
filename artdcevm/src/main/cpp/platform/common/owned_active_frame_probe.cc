// Debug-only real owned fixture bridge, intentionally separate from current callers.
#ifdef ARTDCEVM_TESTING
#include "platform/common/active_frame_recovery_proof.h"
#include "platform/common/owned_active_frame_gate.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/jni_access/owned_evolution_proofs.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/arm64/common/runtime_debuggable.h"
#include "hooks/shadow_hook_engine.h"
#include <atomic>
#include <array>
#include <sstream>
#include <vector>
#include "platform/common/owned_jit_compilation.h"
namespace artdcevm::owned_active_frame {
namespace {
Gate gate;
std::atomic<abi::arm64::android11::CurrentThread> current{nullptr};
using Leaf=abi::arm64::android_common::FromReflectedMethod;
using ContainsMethod=bool (*)(void*,void*);
struct Roots {JNIEnv* env;std::array<jobject,8> refs{};
 ~Roots(){for(auto ref:refs)if(ref)env->DeleteGlobalRef(ref);}};
struct Attempt {active_frame_recovery_proof::Family family;const char* missing;active_frame_recovery_proof::Report report;bool bound;};
struct Request {
 ShadowHookEngine* hooks;Leaf leaf;const Roots* roots;std::array<jint,22> dex{};std::array<jint,3> integers{};
 std::array<void*,5> methods{};method_identity::Calibration calibration;
 void* cache=nullptr;ContainsMethod contains=nullptr;
 active_frame_recovery_proof::Report report;
 std::vector<Attempt> attempts;bool debug_observed=false,java_debuggable=false;
 uint32_t fresh_root_witnesses=0;
 bool physical_callers=false,jit_compiled=false,passed=false;std::string error,model;
 Request(ShadowHookEngine* engine,Leaf native_leaf,const Roots* strong_roots):hooks(engine),leaf(native_leaf),roots(strong_roots){}

};
bool Resolve(const runnable_jni::Context& context,void* data) {
 auto& request=*static_cast<Request*>(data);method_identity::Oracle oracles[5];
 for(size_t i=0;i<5;++i)oracles[i]={request.roots->refs[i],static_cast<uint32_t>(request.dex[i]),static_cast<uint32_t>(request.dex[5+i])};
 if(!method_identity::Calibrate(context,request.leaf,oracles,5,&request.calibration,&request.error))return false;
 abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 for(size_t i=0;i<5;++i){request.methods[i]=request.leaf(scope,request.roots->refs[i]);if(!request.methods[i]){request.error="Missing native reflected fixture method";return false;}}
 return true;
}
struct CompilationRequest {
 Request* identities;const owned_jit_compilation::Service* service;void* jit;
 void* cache=nullptr;ContainsMethod contains=nullptr;bool cached=false;
};
bool QueueCarrier(const runnable_jni::Context& context,void* data) {
 auto& request=*static_cast<CompilationRequest*>(data);
 if(!Resolve(context,request.identities))return false;
 return request.service->Queue(request.jit,request.identities->methods[0],context.self);
}
bool CheckCarrierCached(const runnable_jni::Context& context,void* data) {
 auto& request=*static_cast<CompilationRequest*>(data);
 if(!Resolve(context,request.identities))return false;
 request.cached=request.contains(request.cache,request.identities->methods[0]);return request.cached;
}
std::string Failure(active_frame_recovery_proof::Failure failure) {
 using F=active_frame_recovery_proof::Failure;
 switch(failure){case F::kMissingBinding:return "Missing named frame binding";case F::kMissingContext:return "Missing original native Context";
 case F::kMissingOracle:return "Missing independent fixture vreg/root witnesses";case F::kWalk:return "Missing real native StackVisitor walk";
 case F::kMissingFrames:return "Missing actual compiled carrier or two distinct inlinees";case F::kFrameIdentity:return "Native visitor geometry/carrier/inline identity mismatch";
 case F::kCodeOwnership:return "Missing real JIT/header PC ownership";case F::kDecodedMap:return "Missing complete decoded frame maps";
 case F::kDexPc:return "Actual native/decoded frame DexPc differs from independently parsed child invoke";case F::kInlineFlag:return "Decoded HasInlineInfo disagrees with original native inline callbacks";case F::kTableMemory:return "Decoded native table storage is not fully mapped/readable";case F::kReferenceMask:return "Actual reference stack/register mask mismatch";case F::kNativeRead:return "Actual native vreg/location read unavailable";
 case F::kValueMismatch:return "Actual vreg/root value disagrees with independent fixture";case F::kNone:return "";}
 return "Unknown actual frame proof failure";
}
bool ProveStopped(const exclusive_owned_roots::Context& scope,void* data) {
 auto& request=*static_cast<Request*>(data);using namespace active_frame_recovery_proof;
 void* target=gate.Target();if(!target||target==scope.self){request.error="Missing distinct blocked native worker Thread";return false;}
 const auto& layout=request.calibration.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 for(size_t i=0;i<5;++i) {method_metadata::Identity identity;
  if(!method_metadata::ReadIdentity(layout,request.methods[i],request.calibration.readable_extents[i],&identity)||identity.dex_method_index!=static_cast<uint32_t>(request.dex[i])) {
   request.error="Native fixture identity changed before exclusive proof";return false;
  }
 }
 request.jit_compiled=request.contains(request.cache,request.methods[0]);
 if(!request.jit_compiled){request.error="Carrier was not actually compiled in observed JIT cache";return false;}
 Witness witnesses[3][2];FrameOracle frames[3];
 for(size_t i=0;i<3;++i) {
  const uintptr_t raw=reinterpret_cast<uintptr_t>(scope.roots[5+i]);
  if(!raw||raw>UINT32_MAX){request.error="Strong reference does not fit actual release vreg representation";return false;}
  ++request.fresh_root_witnesses;
  witnesses[i][0]={static_cast<uint16_t>(request.dex[13+i]),Kind::kInt,static_cast<uint32_t>(request.integers[i])};
  witnesses[i][1]={static_cast<uint16_t>(request.dex[16+i]),Kind::kReference,static_cast<uint32_t>(raw)};
  frames[i]={request.methods[i],static_cast<uint32_t>(request.dex[10+i]),witnesses[i],2,static_cast<uint32_t>(request.dex[19+i])};
 }
 Oracle oracle{frames[0],{frames[1],frames[2]},request.physical_callers?Shape::kThreePhysical:Shape::kCarrierTwoInlinees};
 size_t capacity=0;for(size_t i=0;i<3;++i)capacity=std::max(capacity,static_cast<size_t>(request.dex[10+i]));
 // Scratch native memory only; no JNI or managed roots escape this stop.
 std::vector<decoded_frame_recovery::Location> scratch(capacity);
 auto finder=[&](const char* name){return request.hooks->FindSymbol("libart.so",name);};
 // Constructor's original Context/thread echo selects actual native geometry;
 // full maps plus independent active values authorize this particular fixture.
 // Equal13/14/15/16 decoded storage does not authorize their different flags.
 for(Family family:{Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid15,Family::kAndroid16,Family::kAndroid17}) {
  Service service;const char* missing=nullptr;
  if(!Service::Bind(family,finder,&service,&missing)){request.attempts.push_back({family,missing,{},false});continue;}
  auto report=service.Prove(target,request.cache,oracle,scratch.data(),scratch.size(),method_identity::ReadableMappingExtent);
  request.attempts.push_back({family,nullptr,report,true});
  if(report.failure==Failure::kNone && report.frames==3 && report.integers==3 && report.references==3 && report.location_reads==6 && report.dex_pc_checks==3 && (request.physical_callers?(report.physical_matches==3 && report.inline_matches==0 && report.inline_flag_checks==0):report.inline_flag_checks==2)) {
   request.report=report;request.passed=true;
   request.model=family==Family::kAndroid11?"CodeInfo368/Visitor520/legacy-kind":family==Family::kAndroid12?"CodeInfo376/Visitor528":family==Family::kAndroid17?"CodeInfo368/Visitor520/native-location-reader":"CodeInfo344/Visitor496";
   request.error.clear();return true;
  }
  // Prefer an actually observed target/frame failure over a pre-walk wrong ABI.
  if(request.report.failure==Failure::kMissingBinding || report.callbacks>request.report.callbacks || report.frames>request.report.frames ||
     (request.report.failure==Failure::kFrameIdentity && report.failure!=Failure::kFrameIdentity))request.report=report;
 }
 request.error=Failure(request.report.failure);return false;
}
const char* DeclarationName(active_frame_recovery_proof::Family family) {
 using F=active_frame_recovery_proof::Family;
 switch(family){case F::kAndroid11:return "fixed11-CodeInfo368-Visitor520";case F::kAndroid12:return "fixed12-CodeInfo376-Visitor528";
 case F::kAndroid13:return "fixed13-CodeInfo344-Visitor496-legacy-reader";case F::kAndroid14:return "fixed14-CodeInfo344-Visitor496-native-location-reader";
 case F::kAndroid15:return "fixed15-CodeInfo344-Visitor496-native-location-reader";case F::kAndroid16:return "fixed16-CodeInfo344-Visitor496-native-location-reader";
 case F::kAndroid17:return "fixed17-CodeInfo368-Visitor520-native-location-reader";}return "unknown-source";
}
std::string Escape(const std::string& text){std::string result;for(char c:text){if(c=='"'||c=='\\')result+='\\';if(c=='\n'){result+="\\n";continue;}if(static_cast<unsigned char>(c)<32)continue;result+=c;}return result;}
std::string Json(const Request& request){std::ostringstream out;out<<"{\"passed\":"<<(request.passed?"true":"false")<<",\"carrierJitCompiled\":"<<(request.jit_compiled?"true":"false")
 <<",\"frameShape\":\""<<(request.physical_callers?"three-physical":"carrier-two-inlinees")<<"\""
 <<",\"wholeProcessFrameEligibility\":false"
 <<",\"frames\":"<<request.report.frames<<",\"integers\":"<<request.report.integers<<",\"references\":"<<request.report.references<<",\"locationReads\":"<<request.report.location_reads
 <<",\"nativeReaderRoute\":\""<<request.report.reader_route<<"\""
 <<",\"nativeLocationReads\":"<<request.report.native_location_reads
 <<",\"physicalComparisonReads\":"<<request.report.physical_comparison_reads
 <<",\"rawReferenceReads\":"<<request.report.raw_reference_reads
 <<",\"nativeReferenceMaskChecks\":"<<request.report.reference_mask_checks
 <<",\"freshNativeStrongRootWitnesses\":"<<request.fresh_root_witnesses
 <<",\"nativeContextIdentityMask\":"<<request.report.context_identity_mask
 <<",\"nativeContextGetterObserved\":"<<(request.report.native_context_getter_observed?"true":"false")
 <<",\"nativeContextChecks\":"<<request.report.native_context_checks
 <<",\"nativeReferenceReadBarrierExecuted\":false"
 <<",\"dexPcChecks\":"<<request.report.dex_pc_checks<<",\"inlineFlagNativeObserved\":"<<(request.report.inline_flag_checks==2?"true":"false")
 <<",\"walkCallbacks\":"<<request.report.callbacks<<",\"matchedMethodMask\":"<<request.report.matched_mask
 <<",\"physicalMethodMatches\":"<<request.report.physical_matches<<",\"inlineMethodMatches\":"<<request.report.inline_matches
 <<",\"headerOwnershipChecks\":"<<request.report.header_checks<<",\"stage\":\""<<Escape(request.report.detail)<<"\""
 <<",\"runtimeJavaDebuggableObserved\":"<<(request.debug_observed?"true":"false")<<",\"runtimeJavaDebuggable\":"<<(request.java_debuggable?"true":"false")
 <<",\"model\":\""<<Escape(request.model)<<"\",\"error\":\""<<Escape(request.error)<<"\",\"attempts\":[";
 bool first=true;for(const auto& attempt:request.attempts){if(!first)out<<',';first=false;
  out<<"{\"sourceDeclaration\":\""<<DeclarationName(attempt.family)<<"\",\"bound\":"<<(attempt.bound?"true":"false")
   <<",\"missingSymbol\":\""<<Escape(attempt.missing?attempt.missing:"")<<"\",\"stage\":\""<<Escape(attempt.report.detail)
   <<"\",\"walkCallbacks\":"<<attempt.report.callbacks<<",\"matchedMethodMask\":"<<attempt.report.matched_mask
   <<",\"physicalMethodMatches\":"<<attempt.report.physical_matches<<",\"inlineMethodMatches\":"<<attempt.report.inline_matches
   <<",\"physicalOwnerMethodMask\":"<<attempt.report.physical_owner_mask
   <<",\"constructorContextEchoMask\":"<<attempt.report.constructor_context_echo_mask
   <<",\"mapRow\":"<<attempt.report.map_row<<",\"inlineBegin\":"<<attempt.report.inline_begin
   <<",\"inlineEnd\":"<<attempt.report.inline_end<<",\"nativeCarrierRegisters\":"<<attempt.report.native_carrier_registers
   <<",\"registerRangeObserved\":"<<(attempt.report.register_range_observed?"true":"false")
   <<",\"decodedRegisterFirst\":"<<attempt.report.decoded_register_first<<",\"decodedRegisterCount\":"<<attempt.report.decoded_register_count
   <<",\"expectedRegisters\":"<<attempt.report.expected_registers
   <<",\"registerCountSource\":\""<<attempt.report.register_count_source<<"\""
   <<",\"nativeReaderRoute\":\""<<attempt.report.reader_route<<"\""
   <<",\"nativeLocationReads\":"<<attempt.report.native_location_reads
   <<",\"physicalComparisonReads\":"<<attempt.report.physical_comparison_reads
   <<",\"nativeContextChecks\":"<<attempt.report.native_context_checks
   <<",\"nativePcOffsetObserved\":"<<(attempt.report.native_pc_offset_observed?"true":"false")
   <<",\"nativePcOffset\":"<<attempt.report.native_pc_offset
   <<",\"sourcePcOffset\":"<<attempt.report.source_pc_offset
   <<",\"nativeRegisterCountObserved\":"<<(attempt.report.native_register_count_observed?"true":"false")
   <<",\"nativeRegisterCount\":"<<attempt.report.native_register_count
   <<",\"slowRegisterExpectedCount\":"<<attempt.report.slow_register_expected_count
   <<",\"sourceInlineDepth\":"<<attempt.report.source_inline_depth
   <<",\"nativeInlineDepthObserved\":"<<(attempt.report.native_inline_depth_observed?"true":"false")
   <<",\"nativeInlineDepth\":"<<attempt.report.native_inline_depth
   <<",\"frames\":"<<attempt.report.frames<<'}';}
 out<<"]}";return out.str();}
}
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameFixture_awaitGate(JNIEnv*,jclass) {
 auto native_current=artdcevm::owned_active_frame::current.load();
 artdcevm::owned_active_frame::gate.Await(native_current?native_current():nullptr);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_prepareCarrier(JNIEnv* env,jclass,jobjectArray methods,jintArray shape) {
 using namespace artdcevm;using namespace owned_active_frame;
 Roots roots{env};ShadowHookEngine hooks;Request identities(&hooks,nullptr,&roots);
 const char* failure=nullptr;bool queued=false,drained=false,cached=false;
 auto finish=[&](){std::ostringstream out;out<<"{\"passed\":"<<(queued&&drained&&cached&&!failure?"true":"false")
  <<",\"optimizedCompilationRequested\":"<<(queued?"true":"false")<<",\"compilationQueueDrained\":"<<(drained?"true":"false")
  <<",\"carrierCachedAfterDrain\":"<<(cached?"true":"false")<<",\"inlineChainProven\":false,\"error\":\""<<Escape(failure?failure:identities.error)<<"\"}";
  return env->ExceptionCheck()?nullptr:env->NewStringUTF(out.str().c_str());};
 if(!methods||!shape||env->ExceptionCheck()||env->GetArrayLength(methods)!=5||env->GetArrayLength(shape)!=22){failure="Invalid independent compiler fixture request";return finish();}
 env->GetIntArrayRegion(shape,0,22,identities.dex.data());
 for(size_t i=0;i<5;++i){auto local=env->GetObjectArrayElement(methods,static_cast<jsize>(i));roots.refs[i]=local?env->NewGlobalRef(local):nullptr;if(local)env->DeleteLocalRef(local);if(!roots.refs[i]||env->ExceptionCheck())return nullptr;}
 if(!hooks.Initialize(&identities.error))return finish();
 identities.leaf=reinterpret_cast<Leaf>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 auto native_current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 auto contains=reinterpret_cast<ContainsMethod>(hooks.FindSymbol("libart.so","_ZN3art3jit12JitCodeCache14ContainsMethodEPNS_9ArtMethodE"));
 const auto& observed=jni_access::OwnedEvolutionProofs().observed.receivers;
 owned_jit_compilation::Service service;
 if(!identities.leaf||!native_current||!contains||!observed.jit||!observed.jit_code_cache||
    !owned_jit_compilation::Service::Bind([&](const char* name){return hooks.FindSymbol("libart.so",name);},&service)){
  failure="Missing independent JIT receiver or exact optimizing enqueue/wait binding";return finish();
 }
 CompilationRequest request{&identities,&service,observed.jit,observed.jit_code_cache,contains,false};
 if(runnable_executor::With(env,roots.refs[0],QueueCarrier,&request)!=runnable_jni::Result::kSuccess){failure="Owned optimizing enqueue failed";return finish();}
 queued=true;
 // The JNI Runnable scope is destroyed here. Never wait for compiler threads
 // while retaining shared mutator access. CurrentThread is obtained afresh;
 // all five reflected Methods stay strongly rooted until draining returns.
 auto self=native_current();if(!self){failure="Missing current native Thread for compiler drain";return finish();}
 drained=service.Drain(observed.jit,self);
 // Membership alone does not prove optimizing code or inlining. Only the
 // independent suspended physical/two-inline/map/value/root proof below does.
 cached=runnable_executor::With(env,roots.refs[0],CheckCarrierCached,&request)==runnable_jni::Result::kSuccess&&request.cached;
 return finish();
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_armGate(JNIEnv* env,jclass) {
 using namespace artdcevm;ShadowHookEngine hooks;std::string error;
 if(!hooks.Initialize(&error)||!runnable_executor::Initialize(hooks,env,&error))return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());
 auto native_current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 if(!native_current||!native_current())return env->NewStringUTF("Missing real native CurrentThread");
 owned_active_frame::current.store(native_current);
 return env->NewStringUTF(owned_active_frame::gate.Arm()?"":"Native active-frame gate already owned");
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_waitEntered(JNIEnv*,jclass,jint timeout) {
 return timeout>0&&timeout<=30000&&artdcevm::owned_active_frame::gate.Entered(static_cast<unsigned>(timeout));
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_releaseGate(JNIEnv*,jclass) {artdcevm::owned_active_frame::gate.Release();}
static jstring ProveOwnedCallers(JNIEnv* env,jobjectArray methods,jintArray shape,jobjectArray references,jintArray integers,bool physical) {
 using namespace artdcevm;using namespace owned_active_frame;
 if(!methods||!shape||!references||!integers||env->ExceptionCheck()||env->GetArrayLength(methods)!=5||env->GetArrayLength(shape)!=22||env->GetArrayLength(references)!=3||env->GetArrayLength(integers)!=3)return nullptr;
 ShadowHookEngine hooks;Roots roots{env};Request request{&hooks,nullptr,&roots};request.physical_callers=physical;
 env->GetIntArrayRegion(shape,0,22,request.dex.data());env->GetIntArrayRegion(integers,0,3,request.integers.data());if(env->ExceptionCheck())return nullptr;
 for(size_t i=0;i<22;++i)if(request.dex[i]<0||(i>=10&&i<19&&request.dex[i]>UINT16_MAX)){request.error="Invalid independently parsed fixture DEX shape";return env->NewStringUTF(Json(request).c_str());}
 for(size_t i=0;i<8;++i){jobject local=env->GetObjectArrayElement(i<5?methods:references,static_cast<jsize>(i<5?i:i-5));roots.refs[i]=local?env->NewGlobalRef(local):nullptr;if(local)env->DeleteLocalRef(local);if(!roots.refs[i]||env->ExceptionCheck())return nullptr;}
 request.cache=jni_access::OwnedEvolutionProofs().observed.receivers.jit_code_cache;
 if(!request.cache){request.error="Missing independently observed owned JIT cache receiver";return env->NewStringUTF(Json(request).c_str());}
 if(!hooks.Initialize(&request.error))return env->ExceptionCheck()?nullptr:env->NewStringUTF(Json(request).c_str());
 // Fixed native VMRuntime declaration is a read-only runtime policy oracle.
 // It returns a boolean and does not access the unused JNI receiver.
 auto debug_query=reinterpret_cast<abi::arm64::android_common::JavaDebuggableQuery>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kJavaDebuggableQuery));
 if(debug_query){request.debug_observed=true;request.java_debuggable=debug_query(env,nullptr)==JNI_TRUE;}
 request.leaf=reinterpret_cast<Leaf>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 request.contains=reinterpret_cast<ContainsMethod>(hooks.FindSymbol("libart.so","_ZN3art3jit12JitCodeCache14ContainsMethodEPNS_9ArtMethodE"));
 exclusive_owned_roots::Service exclusive;
 if(!request.leaf||!request.contains)request.error="Missing native method/JIT identity leaf";
 else if(!exclusive_owned_roots::Service::Bind(hooks,&exclusive,&request.error)){}
 else if(runnable_executor::With(env,roots.refs[0],Resolve,&request)!=runnable_jni::Result::kSuccess){if(request.error.empty())request.error="Owned fixture method identity calibration failed";}
 else {
  std::string stop_error;
  if(!exclusive.With(env,roots.refs.data(),roots.refs.size(),ProveStopped,&request,&stop_error)) {
   request.passed=false;if(request.error.empty())request.error=stop_error;
  }
 }
 if(env->ExceptionCheck())return nullptr;
 return env->NewStringUTF(Json(request).c_str());
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_prove(JNIEnv* env,jclass,jobjectArray methods,jintArray shape,jobjectArray references,jintArray integers) {
 return ProveOwnedCallers(env,methods,shape,references,integers,false);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_OwnedActiveFrameCalibration_provePhysical(JNIEnv* env,jclass,jobjectArray methods,jintArray shape,jobjectArray references,jintArray integers) {
 return ProveOwnedCallers(env,methods,shape,references,integers,true);
}
#endif

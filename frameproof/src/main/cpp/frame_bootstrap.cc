#include "frame_observation.h"
#include "hooks/shadow_hook_engine.h"
#include "platform/common/runtime_receiver_probe.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/jni_access/owned_evolution_proofs.h"
#include "platform/abi/arm64/android-common/runtime_debuggable.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <array>
#include <mutex>
#include <sstream>
namespace artdcevm::frameproof {
namespace {
Observation observation;
Enqueue enqueue=nullptr;LegacyEnqueue legacy_enqueue=nullptr;
Done11 done11=nullptr;Done12 done12=nullptr;Done14 done14=nullptr;HeapCollect heap_collect=nullptr;HeapTotalMemory heap_total_memory=nullptr;
void* owned_thread=nullptr;
struct MemoryQuery {void* self=nullptr;unsigned calls=0;size_t returned=0;};
thread_local MemoryQuery* active_memory_query=nullptr;
std::mutex installation,operation;
bool attempted=false,installed=false,owned=false,debug_observed=false,java_debuggable=false;
std::array<jobject,6> roots{};std::array<jint,10> dex{};
runtime_receiver::Receivers receivers;
using Leaf=abi::arm64::android_common::FromReflectedMethod;
using Contains=bool (*)(void*,void*);
Leaf leaf=nullptr;Contains contains=nullptr;
void EnqueueProxy(void* receiver,void* method,void* self){observation.Enqueued(receiver,method);enqueue(receiver,method,self);}
bool LegacyEnqueueProxy(void* receiver,void* self,void* method,uint32_t before,uint32_t after,bool backedges){observation.Enqueued(receiver,method);return legacy_enqueue(receiver,self,method,before,after,backedges);}
void Done11Proxy(void* receiver,void* method,void* self,bool osr){done11(receiver,method,self,osr);observation.Completed(receiver,method);}
void Done12Proxy(void* receiver,void* method,void* self,int kind){done12(receiver,method,self,kind);observation.Completed(receiver,method);}
void Done14Proxy(void* receiver,void* method,void* self){done14(receiver,method,self);observation.Completed(receiver,method);}
void HeapProxy(void* receiver,bool clear,int cause){heap_collect(receiver,clear,cause);observation.Collected(receiver);}
size_t HeapTotalProxy(const void* receiver){
 const size_t result=heap_total_memory(receiver);
 if(active_memory_query && active_memory_query->self==owned_thread){
  observation.MemoryQueried(const_cast<void*>(receiver));
  ++active_memory_query->calls;active_memory_query->returned=result;
 }
 return result;
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error){
 std::lock_guard<std::mutex> lock(installation);
 if(installed)return true;
 if(attempted)return Fail(error,"Frame-only observation installation failed; restart process");
 auto e=hooks.FindSymbol("libart.so",kEnqueue);bool legacy=!e;
 if(legacy)e=hooks.FindSymbol("libart.so",kLegacyEnqueue);
 auto d=hooks.FindSymbol("libart.so",kDone14);unsigned variant=14;
 if(!d){d=hooks.FindSymbol("libart.so",kDone12);variant=12;}
 if(!d){d=hooks.FindSymbol("libart.so",kDone11);variant=11;}
 auto h=hooks.FindSymbol("libart.so",kHeapTotalMemory);
 auto collect=hooks.FindSymbol("libart.so",kHeapCollect);
 using namespace abi::arm64::android_common;
 auto suspend=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto resume=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!e||!d||!h||!suspend||!resume)return Fail(error,"Missing named enqueue/DoneCompiling/public totalMemory observation binding");
 attempted=true;SuspendAll stop{};suspend(&stop,"owned nondebug frame observation",false);
 void *eo=nullptr,*dc=nullptr,*ho=nullptr;
 auto et=hooks.Install(e,legacy?reinterpret_cast<void*>(LegacyEnqueueProxy):reinterpret_cast<void*>(EnqueueProxy),&eo,error);
 if(legacy)legacy_enqueue=reinterpret_cast<LegacyEnqueue>(eo);else enqueue=reinterpret_cast<Enqueue>(eo);
 void* proxy=variant==11?reinterpret_cast<void*>(Done11Proxy):variant==12?reinterpret_cast<void*>(Done12Proxy):reinterpret_cast<void*>(Done14Proxy);
 auto dt=et&&eo?hooks.Install(d,proxy,&dc,error):nullptr;
 if(variant==11)done11=reinterpret_cast<Done11>(dc);else if(variant==12)done12=reinterpret_cast<Done12>(dc);else done14=reinterpret_cast<Done14>(dc);
 auto ht=dt&&dc?hooks.Install(h,reinterpret_cast<void*>(HeapTotalProxy),&ho,error):nullptr;
 heap_total_memory=reinterpret_cast<HeapTotalMemory>(ho);installed=et&&dt&&ht&&eo&&dc&&ho;
 // Collection entry can be optimized out of a public GC route. It remains an
 // optional passthrough observation, never a substitute for completion stats.
 if(installed&&collect){void* original=nullptr;
  auto token=hooks.Install(collect,reinterpret_cast<void*>(HeapProxy),&original,error);
  if(token&&original)heap_collect=reinterpret_cast<HeapCollect>(original);
  else installed=false;
 }
 resume(&stop);return installed||Fail(error,"Unable to install passthrough frame observation hooks; restart process");
}
void Release(JNIEnv* env){
 observation.expected=nullptr;observation.gc_active=false;
 for(auto& root:roots){if(root)env->DeleteGlobalRef(root);root=nullptr;}
 owned_thread=nullptr;owned=false;
}
struct ReadRequest {void* runtime=nullptr;void* method=nullptr;void* self=nullptr;bool compiled=false;runtime_services::View view;std::string error;};
bool Resolve(const runnable_jni::Context& context,void* opaque){
 auto& request=*static_cast<ReadRequest*>(opaque);method_identity::Oracle oracles[5];
 for(size_t i=0;i<5;++i)oracles[i]={roots[i],static_cast<uint32_t>(dex[i]),static_cast<uint32_t>(dex[5+i])};
 method_identity::Calibration calibration;
 if(!method_identity::Calibrate(context,leaf,oracles,5,&calibration,&request.error))return false;
 abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 request.self=context.self;request.method=leaf(scope,roots[0]);return request.method&&!context.env->ExceptionCheck();
}
bool QueryPublicHeap(const runnable_jni::Context& context,void* opaque){
 auto& request=*static_cast<ReadRequest*>(opaque);
 if(!owned_thread||context.self!=owned_thread)return Fail(&request.error,"Public memory oracle owner changed");
 auto klass=context.env->FindClass("java/lang/Runtime");
 auto getter=klass?context.env->GetStaticMethodID(klass,"getRuntime","()Ljava/lang/Runtime;"):nullptr;
 auto total=klass?context.env->GetMethodID(klass,"totalMemory","()J"):nullptr;
 auto runtime=getter&&total?context.env->CallStaticObjectMethod(klass,getter):nullptr;
 MemoryQuery query{context.self,0,0};
 jlong bytes=0;
 if(runtime&&!context.env->ExceptionCheck()){
  active_memory_query=&query;bytes=context.env->CallLongMethod(runtime,total);active_memory_query=nullptr;
 }
 if(runtime)context.env->DeleteLocalRef(runtime);
 if(klass)context.env->DeleteLocalRef(klass);
 if(context.env->ExceptionCheck()||!runtime||bytes<=0||!query.calls||
    query.returned!=static_cast<uint64_t>(bytes)||observation.conflict.load()){
  std::ostringstream out;out<<"Public totalMemory oracle failed: callbacks="<<query.calls
   <<" positiveResult="<<(bytes>0)<<" resultAgrees="<<(bytes>0&&query.returned==static_cast<uint64_t>(bytes))
   <<" receiverConflict="<<observation.conflict.load();request.error=out.str();return false;
 }
 return true;
}
bool Match(const runnable_jni::Context&,void* opaque){
 auto& request=*static_cast<ReadRequest*>(opaque);size_t extent=0;
 if(!method_identity::ReadableMappingExtent(request.runtime,&extent))return false;
 runtime_services::View observed;
 observed.class_linker=receivers.class_linker;observed.jni_id_manager=receivers.jni_id_manager;
 observed.thread_list=receivers.thread_list;observed.java_vm=receivers.java_vm;
 observed.jit=observation.jit.load();observed.jit_code_cache=observation.cache.load();observed.heap=observation.heap.load();
 for(auto declaration:{runtime_services::Declaration::kAndroid11,runtime_services::Declaration::kAndroid12,runtime_services::Declaration::kAndroid13,runtime_services::Declaration::kAndroid14,runtime_services::Declaration::kAndroid15To17}){
  runtime_services::View view;
  if(runtime_services::Read(declaration,request.runtime,extent,&view)&&Same(view,observed)){
   request.view=view;request.compiled=contains(observed.jit_code_cache,request.method);return request.compiled;
  }
 }
 return false;
}
std::string Escape(const std::string& text){std::string out;for(char c:text){if(c=='"'||c=='\\')out+='\\';if(c=='\n'){out+="\\n";continue;}if(static_cast<unsigned char>(c)>=32)out+=c;}return out;}
std::string Json(bool passed,bool compiled,jlong gc,const std::string& error){
 std::ostringstream out;out<<"{\"passed\":"<<(passed?"true":"false")<<",\"runtimeJavaDebuggableObserved\":"<<(debug_observed?"true":"false")<<",\"runtimeJavaDebuggable\":"<<(java_debuggable?"true":"false")<<",\"runtimeReceiversMatched\":"<<(passed?"true":"false")
 <<",\"methodIdentitiesChecked\":"<<(passed?5:0)<<",\"carrierJitCompiled\":"<<(compiled?"true":"false")<<",\"enqueueCalls\":"<<observation.enqueues.load()<<",\"compileCompletionCalls\":"<<observation.completions.load()
 <<",\"heapMemoryQueryCalls\":"<<observation.memory_queries.load()<<",\"heapCollectionCalls\":"<<observation.collections.load()<<",\"gcCompletionDelta\":"<<gc<<",\"publicJvmtiInitialized\":false,\"error\":\""<<Escape(error)<<"\"}";return out.str();
}
}
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_FrameProofBootstrap_begin(JNIEnv* env,jclass,jobjectArray methods,jintArray shape,jclass carrier){
 using namespace artdcevm;using namespace frameproof;
 std::unique_lock<std::mutex> lock(operation,std::try_to_lock);std::string error;
 if(!lock.owns_lock()||owned)return env->NewStringUTF("Frame calibration already owned");
 if(!methods||!shape||!carrier||env->ExceptionCheck()||env->GetArrayLength(methods)!=5||env->GetArrayLength(shape)!=22)return env->NewStringUTF("Invalid independent frame DEX/method request");
 // This target intentionally does not initialize the public ART/JVMTI backend.
 ShadowHookEngine hooks;
 if(!hooks.Initialize(&error)||!runnable_executor::Initialize(hooks,env,&error))return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());
 auto debug=reinterpret_cast<abi::arm64::android_common::JavaDebuggableQuery>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kJavaDebuggableQuery));
 debug_observed=debug!=nullptr;java_debuggable=debug&&debug(env,nullptr)!=JNI_FALSE;
 if(!debug||java_debuggable)return env->NewStringUTF("Missing read-only native nondebuggable runtime policy proof");
 if(!runtime_receiver::Calibrate(hooks,env,&receivers,&error)||!Install(hooks,&error))return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());
 leaf=reinterpret_cast<Leaf>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 contains=reinterpret_cast<Contains>(hooks.FindSymbol("libart.so","_ZN3art3jit12JitCodeCache14ContainsMethodEPNS_9ArtMethodE"));
 if(!leaf||!contains)return env->NewStringUTF("Missing named method/compiled JIT identity leaf");
 env->GetIntArrayRegion(shape,0,10,dex.data());for(auto value:dex)if(value<0)return env->NewStringUTF("Invalid independent method metadata");
 owned=true;
 for(size_t i=0;i<5;++i){auto local=env->GetObjectArrayElement(methods,static_cast<jsize>(i));roots[i]=local?env->NewGlobalRef(local):nullptr;if(local)env->DeleteLocalRef(local);if(!roots[i]||env->ExceptionCheck()){Release(env);return nullptr;}}
 roots[5]=env->NewGlobalRef(carrier);if(!roots[5]||env->ExceptionCheck()){Release(env);return nullptr;}
 ReadRequest request;
 if(runnable_executor::With(env,roots[0],Resolve,&request)!=runnable_jni::Result::kSuccess){Release(env);return env->ExceptionCheck()?nullptr:env->NewStringUTF(request.error.empty()?"Actual five method identity calibration failed":request.error.c_str());}
 // Public JNI method ID agrees with the reflected carrier; never decode this ID.
 auto reflected=env->FromReflectedMethod(roots[0]);auto named=env->GetStaticMethodID(carrier,"carrier","(ILjava/lang/Object;ILjava/lang/Object;ILjava/lang/Object;)I");
 if(!reflected||reflected!=named||env->ExceptionCheck()){Release(env);return env->ExceptionCheck()?nullptr:env->NewStringUTF("Public carrier JNI/reflection identity mismatch");}
 observation.jit=nullptr;observation.cache=nullptr;observation.heap=nullptr;observation.conflict=false;
 observation.enqueues=0;observation.completions=0;observation.collections=0;observation.memory_queries=0;
 owned_thread=request.self;
 observation.expected=request.method;observation.gc_active=true;
 if(runnable_executor::With(env,roots[0],QueryPublicHeap,&request)!=runnable_jni::Result::kSuccess){Release(env);return env->ExceptionCheck()?nullptr:env->NewStringUTF(request.error.c_str());}
 jni_access::OwnedEvolutionProofs().observed={};
 return env->NewStringUTF("");
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_FrameProofBootstrap_finish(JNIEnv* env,jclass,jlong gc_delta){
 using namespace artdcevm;using namespace frameproof;
 std::lock_guard<std::mutex> lock(operation);std::string error;bool passed=false;ReadRequest request;
 if(!owned)error="Missing owned frame calibration";
 else if(runnable_executor::With(env,roots[0],QueryPublicHeap,&request)!=runnable_jni::Result::kSuccess)error=request.error;
 else if(!observation.Complete()||gc_delta<=0)error="Missing real carrier enqueue/completion/public Heap queries or completed public GC proof";
 else {
  ShadowHookEngine hooks;auto singleton=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));size_t extent=0;
  if(!singleton||!method_identity::ReadableMappingExtent(singleton,&extent)||extent<sizeof(void*)||!(request.runtime=*singleton))error="Missing readable named Runtime singleton";
  else if(runnable_executor::With(env,roots[0],Resolve,&request)!=runnable_jni::Result::kSuccess)error="Method identities changed after public GC";
  else if(runnable_executor::With(env,roots[0],Match,&request)!=runnable_jni::Result::kSuccess)error="Runtime seven receivers or actual carrier JIT cache membership mismatch";
  else if(observation.conflict.load())error="Asynchronous native receiver conflict";
  else {jni_access::OwnedEvolutionProofs().observed={request.runtime,nullptr,request.view};passed=true;}
 }
 auto json=Json(passed,request.compiled,gc_delta,error);Release(env);
 return env->ExceptionCheck()?nullptr:env->NewStringUTF(json.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_FrameProofBootstrap_cancel(JNIEnv* env,jclass){
 using namespace artdcevm::frameproof;std::lock_guard<std::mutex> lock(operation);Release(env);
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_testing_FrameProofBootstrap_ready(JNIEnv*,jclass){
 return artdcevm::frameproof::observation.Ready()?JNI_TRUE:JNI_FALSE;
}

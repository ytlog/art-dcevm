#include "platform/common/owned_jit_receivers.h"
#include "platform/common/heap_collect_observer.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <mutex>
#include <jvmti.h>
namespace artdcevm::owned_jit_receivers {
namespace {
// Fixed b753cf97923c jit.h, jit_code_cache.h, heap.h. GcCause is a
// source enum with the default int underlying representation.
using Enqueue=void (*)(void*,void*,void*);
using LegacyEnqueue=bool (*)(void*,void*,void*,uint32_t,uint32_t,bool);
using Collect=void (*)(void*);
constexpr char kEnqueue[]="_ZN3art3jit3Jit23MaybeEnqueueCompilationEPNS_9ArtMethodEPNS_6ThreadE";
constexpr char kLegacyEnqueue[]="_ZN3art3jit3Jit18MaybeCompileMethodEPNS_6ThreadEPNS_9ArtMethodEjjb";
constexpr char kCollect[]="_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv";
Enqueue enqueue=nullptr;LegacyEnqueue legacy_enqueue=nullptr;Collect collect=nullptr;
std::mutex installation,operation;
bool attempted=false,installed=false;
// Process lifetime observation state avoids borrowing a stack Request from an
// asynchronous compilation/GC worker. Every field accessed by hooks is atomic.
std::atomic<void*> expected_method{nullptr},jit{nullptr},cache{nullptr},heap{nullptr};
std::atomic<bool> gc_active{false},conflict{false};
void Observe(std::atomic<void*>& slot,void* value){
  void* empty=nullptr;
  if(!value||(!slot.compare_exchange_strong(empty,value)&&empty!=value))conflict=true;
}
void EnqueueProxy(void* receiver,void* method,void* self){
  if(method&&method==expected_method.load())Observe(jit,receiver);
  enqueue(receiver,method,self);
}
// Fixed Android11/12 jit.h: bool result and both uint32 counters are forwarded
// exactly. AddSamples is inline; the named callee receives the real Jit owner.
bool LegacyEnqueueProxy(void* receiver,void* self,void* method,uint32_t before,uint32_t after,bool backedges){
  if(method&&method==expected_method.load())Observe(jit,receiver);
  return legacy_enqueue(receiver,self,method,before,after,backedges);
}
void CollectProxy(void* receiver){
  if(gc_active.load())Observe(cache,receiver);
  collect(receiver);
}
void ObserveHeap(void* receiver,bool,heap_collect_observer::Cause){
  if(gc_active.load())Observe(heap,receiver);
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error){
  std::lock_guard<std::mutex> guard(installation);
  if(installed)return true;
  if(attempted)return Fail(error,"JIT receiver observation failed; restart process");
  auto e=hooks.FindSymbol("libart.so",kEnqueue);
  const bool legacy=!e;
  if(legacy)e=hooks.FindSymbol("libart.so",kLegacyEnqueue);
  auto c=hooks.FindSymbol("libart.so",kCollect);
  using namespace abi::common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!e||!c||!construct||!destroy)return Fail(error,"Missing named JIT/cache/heap observation capability");
  attempted=true;
  if(!heap_collect_observer::Install(hooks,ObserveHeap,error))return false;
  SuspendAll storage{};construct(&storage,"artdcevm JIT/cache/heap receiver observation",false);
  void *eo=nullptr,*co=nullptr;
  auto et=hooks.Install(e,legacy?reinterpret_cast<void*>(LegacyEnqueueProxy):reinterpret_cast<void*>(EnqueueProxy),&eo,error);
  if(legacy)legacy_enqueue=reinterpret_cast<LegacyEnqueue>(eo);else enqueue=reinterpret_cast<Enqueue>(eo);
  auto ct=et&&eo?hooks.Install(c,reinterpret_cast<void*>(CollectProxy),&co,error):nullptr;
  collect=reinterpret_cast<Collect>(co);
  installed=et&&ct&&eo&&collect;
  destroy(&storage);
  return installed||Fail(error,"Unable to install JIT receiver observers; restart process");
}
struct Read {
  void* runtime;const method_identity::Oracle* oracle;
  abi::common::FromReflectedMethod leaf;
  runtime_services::Snapshot observed;
  method_identity::Model model;
  runtime_services::View view;void* method=nullptr;
};
bool Prepare(const runnable_jni::Context& context,void* opaque){
  auto& r=*static_cast<Read*>(opaque);
  abi::common::RunnableJniScope scope{context.self,context.env,context.vm};
  if(context.env->GetObjectRefType(r.oracle->strong_global_method)!=JNIGlobalRefType)return false;
  r.method=r.leaf(scope,r.oracle->strong_global_method);
  size_t extent=0;method_metadata::Identity identity;
  return !context.env->ExceptionCheck()&&method_identity::ReadableMappingExtent(r.method,&extent)&&
      method_metadata::ReadIdentity(r.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward,r.method,extent,&identity)&&
      identity.dex_method_index==r.oracle->dex_method_index&&
      (identity.access_flags&0x11fu)==(r.oracle->java_modifiers&0x11fu);
}
bool ReadAfterObservation(const runnable_jni::Context&,void* opaque){
  auto& r=*static_cast<Read*>(opaque);size_t extent=0;
  if(!method_identity::ReadableMappingExtent(r.runtime,&extent))return false;
  // Each fixed source candidate must match seven independently observed actual
  // native receivers. Identical source geometries authorize the same View;
  // neither the API number nor an unobserved Runtime pointer is an oracle.
  for(auto candidate:{runtime_services::Declaration::kAndroid11,runtime_services::Declaration::kAndroid12,
      runtime_services::Declaration::kAndroid13,runtime_services::Declaration::kAndroid14,
      runtime_services::Declaration::kAndroid15To17}){
    runtime_services::View view;
    if(runtime_services::Read(candidate,r.runtime,extent,&view)&&
       view.jit==jit.load()&&view.jit_code_cache==cache.load()&&view.heap==heap.load()&&
       view.class_linker==r.observed.receivers.class_linker&&
       view.jni_id_manager==r.observed.receivers.jni_id_manager&&
       view.thread_list==r.observed.receivers.thread_list&&view.java_vm==r.observed.receivers.java_vm){r.view=view;return true;}
  }
  return false;
}
}
bool CalibrateJitReceivers(HookEngine& hooks,JNIEnv* env,jobject strong_class,
    const method_identity::Oracle& hot,const method_identity::Calibration& model,
    const runtime_services::Snapshot& services,jclass additive_target,const unsigned char* additive_dex,size_t dex_size,
    runtime_services::View* output,std::string* error){
  std::unique_lock<std::mutex> guard(operation,std::try_to_lock);
  if(!guard.owns_lock()||!env||!strong_class||!output||!services.runtime||
      !services.receivers.java_vm||!additive_target||!additive_dex||!dex_size||dex_size>INT32_MAX||(model.model!=method_identity::Model::kMethod12Onward&&model.model!=method_identity::Model::kMethod11)||
      model.identities.size()<3||env->ExceptionCheck())return Fail(error,"Invalid owned JIT receiver request");
  if(!Install(hooks,error))return false;
  auto leaf=reinterpret_cast<abi::common::FromReflectedMethod>(
      hooks.FindSymbol("libart.so",abi::common::kFromReflectedMethod));
  if(!leaf)return Fail(error,"Missing named reflected hot-method identity capability");
  Read request{services.runtime,&hot,leaf,services,model.model,{}};
  if(runnable_executor::With(env,strong_class,Prepare,&request)!=runnable_jni::Result::kSuccess)
    return Fail(error,"Owned hot-method DEX/reflection/native identity mismatch");
  jmethodID method=env->FromReflectedMethod(hot.strong_global_method);
  jmethodID expected=env->GetStaticMethodID(static_cast<jclass>(strong_class),"hotLoop","(I)I");
  if(!method||method!=expected||(hot.java_modifiers&0x108u)!=8u||env->ExceptionCheck())return Fail(error,"Public hot-method JNI identity unavailable");
  jit=nullptr;cache=nullptr;heap=nullptr;conflict=false;
  expected_method=request.method;
  volatile jint sink=0;
  for(int i=0;i<32&&!env->ExceptionCheck();++i)
    sink^=env->CallStaticIntMethod(static_cast<jclass>(strong_class),method,100000);
  (void)sink;
  expected_method=nullptr;
  if(env->ExceptionCheck())return Fail(error,"Owned Java JIT fixture failed");
  if(!jit.load()||conflict.load())return Fail(error,"Real Java execution did not establish a consistent JIT receiver");
  jvmtiEnv* temporary=nullptr;
  if(static_cast<JavaVM*>(services.receivers.java_vm)->GetEnv(reinterpret_cast<void**>(&temporary),JVMTI_VERSION_1_2)!=JNI_OK||!temporary)
    return Fail(error,"Independent JVMTI GC environment unavailable");
  gc_active=true;
  jvmtiError gc=temporary->ForceGarbageCollection();
  gc_active=false;
  // A fresh owned additive fixture is redefined through the public native ART
  // extension. Its original structural commit naturally invalidates the cache;
  // never pass a guessed Runtime cache pointer to InvalidateAllCompiledCode.
  jvmtiCapabilities capabilities{};capabilities.can_redefine_classes=1;
  jvmtiError structural=temporary->AddCapabilities(&capabilities);
  jint function_count=0;jvmtiExtensionFunctionInfo* functions=nullptr;
  using Structural=jvmtiError (*)(jvmtiEnv*,jint,const jvmtiClassDefinition*);
  Structural redefine=nullptr;
  if(structural==JVMTI_ERROR_NONE)structural=temporary->GetExtensionFunctions(&function_count,&functions);
  if(structural==JVMTI_ERROR_NONE)for(jint i=0;i<function_count;++i)
    if(functions[i].id&&!std::strcmp(functions[i].id,"com.android.art.class.structurally_redefine_classes"))
      redefine=reinterpret_cast<Structural>(functions[i].func);
  if(functions){
    for(jint i=0;i<function_count;++i){
      auto& f=functions[i];
      if(f.id)temporary->Deallocate(reinterpret_cast<unsigned char*>(f.id));
      if(f.short_description)temporary->Deallocate(reinterpret_cast<unsigned char*>(f.short_description));
      for(jint j=0;j<f.param_count;++j)if(f.params[j].name)temporary->Deallocate(reinterpret_cast<unsigned char*>(f.params[j].name));
      if(f.params)temporary->Deallocate(reinterpret_cast<unsigned char*>(f.params));
      if(f.errors)temporary->Deallocate(reinterpret_cast<unsigned char*>(f.errors));
    }
    temporary->Deallocate(reinterpret_cast<unsigned char*>(functions));
  }
  if(redefine){
    jvmtiClassDefinition definition{additive_target,static_cast<jint>(dex_size),additive_dex};
    gc_active=true;structural=redefine(temporary,1,&definition);gc_active=false;
  }else structural=JVMTI_ERROR_NOT_AVAILABLE;
  const auto disposed=temporary->DisposeEnvironment();
  if(disposed!=JVMTI_ERROR_NONE)return Fail(error,"Owned public JIT/GC calibration cleanup failed");
  if(structural!=JVMTI_ERROR_NONE)return Fail(error,"Owned public additive cache-calibration transaction failed");
  if(gc!=JVMTI_ERROR_NONE||!cache.load()||!heap.load()||conflict.load())
    return Fail(error,(std::string("Public GC receiver observation: cache=")+(cache.load()?"observed":"missing")+", heap="+(heap.load()?"observed":"missing")+", conflict="+(conflict.load()?"true":"false")).c_str());
  if(runnable_executor::With(env,strong_class,ReadAfterObservation,&request)!=runnable_jni::Result::kSuccess)
    return Fail(error,"Observed JIT/cache/heap disagree with candidate Runtime slots");
  // Also retain every earlier independently observed receiver in this snapshot.
  if(request.view.class_linker!=services.receivers.class_linker||
      request.view.jni_id_manager!=services.receivers.jni_id_manager||
      request.view.thread_list!=services.receivers.thread_list||
      request.view.java_vm!=services.receivers.java_vm)return Fail(error,"Runtime service generation changed during JIT calibration");
  *output=request.view;if(error)error->clear();return true;
}
}

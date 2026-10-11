#include "platform/common/production_source_calibration.h"
#include "platform/common/production_final_dex.h"
#include "platform/common/thread_state_calibration.h"
#include "platform/common/native_typed_heap_calibration.h"
#include "platform/common/runnable_executor.h"
#include <mutex>
#include <new>
namespace artdcevm::production_bootstrap {
namespace {
std::mutex preparation;
// Intentionally immortal: installed ART hooks retain service addresses. VM
// teardown cannot run a JNI-root destructor on an unattached native thread.
SourceCalibration*process=nullptr;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
}
struct SourceCalibration::Round {
 SourceCalibration&owner;jvmtiEnv*ti;jvmti_extensions::Structural structural;
 size_t index;std::string diagnostic;
 bool attempted=false,returned=false,started=false;
 jvmtiError result=JVMTI_ERROR_INTERNAL;
};
SourceCalibration*SourceCalibration::Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 std::unique_lock<std::mutex>lock(preparation,std::try_to_lock);
 if(!lock.owns_lock()){Fail(error,"source-calibration-preparation-busy-or-reentrant");return nullptr;}
 if(!env||!ti||!fixture||env->ExceptionCheck()){Fail(error,"source-calibration-invalid-caller");return nullptr;}
 try{
  if(!process){
   auto metadata=MetadataCalibration::Create(hooks,env,ti,fixture,error);if(!metadata)return nullptr;
   process=new SourceCalibration(hooks,std::move(metadata));
  }
  const auto&runtime=process->metadata_->Runtime();JavaVM*vm=nullptr;
  if(&process->hooks_!=&hooks||env->GetJavaVM(&vm)!=JNI_OK||vm!=runtime.receivers.java_vm||env->ExceptionCheck()){
   Fail(error,"source-calibration-process-owner-or-VM-differs");return nullptr;
  }
  auto actual=env->GetObjectClass(fixture);
  const bool anchor=actual&&!env->ExceptionCheck()&&env->IsSameObject(actual,process->metadata_->Inputs().Anchor());
  if(actual)env->DeleteLocalRef(actual);
  if(!anchor||env->ExceptionCheck()){Fail(error,"source-calibration-fixture-Class-differs");return nullptr;}
  redefinition_gate::Lease lease(vm);
  if(!lease.Acquired()||!BorrowFinalDexRecords(vm)){Fail(error,"source-calibration-final-DEX-history-or-lease-unavailable");return nullptr;}
  if(!process->Install(env,error)||!process->Calibrate(env,ti,error)||!process->PrepareCommitReaders(env,error)||!process->PrepareTypedHeap(env,error)||!process->PrepareRetirement(env,ti,error)||!process->PrepareInterpretation(env,error)||!process->PrepareReferences(env,error))return nullptr;
  error->clear();return process;
 }catch(const std::bad_alloc&){Fail(error,"source-calibration-native-allocation-failed");return nullptr;}
}
bool SourceCalibration::Install(JNIEnv*env,std::string*error){
 const auto&runtime=metadata_->Runtime();const auto&inputs=metadata_->Inputs();
 if(!bound_){
  if(!thread_state_calibration::Calibrate(hooks_,env,inputs.Anchor(),&state_,error)||
     !native_source_stop::Install(hooks_,error)||
     !native_source_stop::Service::Bind(hooks_,runtime.receivers.heap,state_,&stop_,error))return false;
  bound_=true;
 }
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks_.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 void*self=current?current():nullptr;size_t extent=0;uint32_t state=UINT32_MAX;
 if(!self||!method_identity::ReadableMappingExtent(self,&extent)||!state_.State(self,extent,&state)||
    state!=abi::arm64::android11::kNative||current()!=self)
  return Fail(error,"source-calibration-installation-needs-actual-Native-thread");
 if(installed_)return true;
 if(stop_.RequiresOriginalWaiting()&&!native_source_stop::InstallOriginalWaitingScope(hooks_,error))return false;
 // This is the current installation call's actual Native context; it is not
 // retained and does not substitute for a later original Runnable callback.
 const runnable_jni::Context native{self,env,runtime.receivers.java_vm};
 if(!native_owned_removal::Install(hooks_,error)||
    !observers_.Install(hooks_,env,runtime,state_,self,extent,error)||
    !schema_.Prepare(hooks_,metadata_->Storage(),metadata_->Fields(),runtime,native,error)||
    !schema_.InstallHooks(native,native_owned_removal::ObserveSourceFailure,error))return false;
 installed_=true;return true;
}
bool SourceCalibration::Calibrate(JNIEnv*env,jvmtiEnv*ti,std::string*error){
 if(poisoned_)return Fail(error,"source-calibration-prior-unknown-or-invalid-completed-round");
 jvmti_extensions::Structural structural=nullptr;
 const auto extension=jvmti_extensions::FindStructural(ti,&structural);
 if(extension!=JVMTI_ERROR_NONE)return Fail(error,"source-calibration-public-structural-extension-unavailable");
 const auto&inputs=metadata_->Inputs();
 if(!inputs.VerifyOrdinary(env,ti,completed_,error))return false;
 while(completed_<2){
  Round round{*this,ti,structural,completed_+1,{}};
  runnable_jni::Result scope;
  try{scope=runnable_executor::WithOriginalScope(env,inputs.OrdinaryClass(),Observe,&round);}
  catch(...){if(round.attempted)poisoned_=true;throw;}
  if(scope!=runnable_jni::Result::kSuccess||!round.returned||round.result!=JVMTI_ERROR_NONE||env->ExceptionCheck()){
   if(round.attempted&&(!round.returned||round.result==JVMTI_ERROR_NONE))poisoned_=true;
   if(error)*error=round.diagnostic.empty()?"source-calibration-original-extension-did-not-complete":round.diagnostic;
   return false;
  }
  // Actual mutation already completed. Never repeat this round if its public
  // result oracle refuses. Bridge/source views closed before any Java oracle.
  completed_=round.index;
  if(!inputs.VerifyOrdinary(env,ti,completed_,error)){poisoned_=true;return false;}
 }
 if(!stop_.Calibrated())return Fail(error,"source-calibration-original-stop-lifecycle-not-proven");
 if(!schema_.Ready())return Fail(error,schema_.Diagnostic());
 return true;
}
bool SourceCalibration::Observe(const runnable_jni::Context&context,void*raw){
 auto&round=*static_cast<Round*>(raw);auto&owner=round.owner;
 if(round.attempted||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    context.vm!=owner.metadata_->Runtime().receivers.java_vm||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm)))
  return Fail(&round.diagnostic,"source-calibration-exact-original-context-unavailable");
 size_t extent=0;
 if(!method_identity::ReadableMappingExtent(context.self,&extent))return false;
 native_source_calibration::RunLifecycle lifecycle{&round,Begin,End};
 native_source_calibration::Bridge bridge(owner.stop_,context.self,extent,owner.observers_.Loading(),context,lifecycle);
 if(!bridge.Registered())return Fail(&round.diagnostic,"source-calibration-original-observer-registration-refused");
 const auto&inputs=owner.metadata_->Inputs();const auto&dex=inputs.OrdinaryDex()[round.index];
 jvmtiClassDefinition definition{inputs.OrdinaryClass(),static_cast<jint>(dex.size()),dex.data()};
 round.attempted=true;
 // The public extension owns transforms, the original Run and all mutation.
 // No private Request or replacement commit service is active in calibration.
 round.result=round.structural(round.ti,1,&definition);round.returned=true;
 if(bridge.Authorized())owner.stop_=bridge.Binding();
 else if(round.diagnostic.empty())round.diagnostic=bridge.Failure();
 return true;
}
struct SourceCalibration::RootLocks {
 SourceCalibration&owner;std::string*error;
};
bool SourceCalibration::ObserveRootLocks(const runnable_jni::Context&context,void*raw){
 auto&request=*static_cast<RootLocks*>(raw);auto&owner=request.owner;
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||
    context.vm!=owner.metadata_->Runtime().receivers.java_vm||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm)))
  return Fail(request.error,"source-commit-readers-original-root-lock-context-unavailable");
 return owner.roots_.CalibrateLocks(context,request.error);
}
bool SourceCalibration::PrepareCommitReaders(JNIEnv*env,std::string*error){
 const auto&runtime=metadata_->Runtime();
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks_.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 void*self=current?current():nullptr;size_t extent=0;uint32_t state=UINT32_MAX;
 if(!env||env->ExceptionCheck()||!self||!method_identity::ReadableMappingExtent(self,&extent)||
    !state_.State(self,extent,&state)||state!=abi::arm64::android11::kNative||current()!=self||
    !BorrowFinalDexRecords(static_cast<JavaVM*>(runtime.receivers.java_vm)))
  return Fail(error,"source-commit-readers-need-actual-Native-owner-and-final-DEX-lease");
 // Each binding publishes only after success. Retain partial progress across
 // installation failure; never rebind an original operation or hook owner.
 if(!commit_bound_){if(!commit_.Bind(hooks_,runtime,error))return false;commit_bound_=true;}
 if(!reflection_bound_){
  if(!reflection_.Bind(hooks_,runtime.runtime,metadata_->ExecutableMirrors(),error))return false;
  reflection_bound_=true;
 }
 if(!cache_bound_){if(!cache_.Bind(hooks_,runtime.receivers.thread_list,error))return false;cache_bound_=true;}
 if(!roots_bound_){if(!roots_.Bind(hooks_,runtime.runtime,current,error))return false;roots_bound_=true;}
 if(!interpretation_bound_){
  if(!interpretation_.BindQuery(hooks_,env,error))return false;
  interpretation_bound_=true;
 }
 if(!cache_.Prepare(error)||!cache_.PreparedForCurrentThread())return false;
 if(!roots_.LocksCalibrated()){
  RootLocks request{*this,error};
  const auto result=runnable_executor::WithOriginalScope(env,metadata_->Inputs().Anchor(),ObserveRootLocks,&request);
  if(result!=runnable_jni::Result::kSuccess||env->ExceptionCheck()||!roots_.LocksCalibrated())
   return Fail(error,"source-commit-readers-original-root-lock-calibration-did-not-complete");
 }
 // No original callback, Thread, mapping extent or root cursor is retained.
 if(current()!=self||!state_.State(self,extent,&state)||state!=abi::arm64::android11::kNative||env->ExceptionCheck())
  return Fail(error,"source-commit-readers-did-not-return-to-original-Native-owner");
 return true;
}
bool SourceCalibration::PrepareTypedHeap(JNIEnv*env,std::string*error){
 const auto&runtime=metadata_->Runtime();
 if(!env||env->ExceptionCheck()||!schema_.Ready()||!stop_.Calibrated()||
    !BorrowFinalDexRecords(static_cast<JavaVM*>(runtime.receivers.java_vm)))
  return Fail(error,"source-typed-heap-needs-calibrated-source-and-actual-final-DEX-lease");
 if(!exclusive_bound_){
  if(!exclusive_owned_roots::Service::Bind(hooks_,&exclusive_,error))return false;
  exclusive_bound_=true;
 }
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks_.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 return native_typed_heap_calibration::Prepare(hooks_,env,metadata_->Inputs().Storage(),
  runtime.receivers.heap,current,metadata_->Fields(),exclusive_,&heap_,error);
}
struct SourceCalibration::Allocator {
 SourceCalibration&owner;jobject boot_method;
 field_mirror::NamedField field_leaf;
 std::vector<void*>owned,foreign;std::string*error;
};
bool SourceCalibration::CollectAllocator(const runnable_jni::Context&context,void*raw){
 auto&request=*static_cast<Allocator*>(raw);auto&owner=request.owner;
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||!context.original_leaf||
    context.vm!=owner.metadata_->Runtime().receivers.java_vm||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm)))
  return Fail(request.error,"source-allocator-original-context-unavailable");
 const abi::common::RunnableJniScope scope{context.self,context.env,context.vm};
 const auto&inputs=owner.metadata_->Inputs();
 for(const auto&oracle:inputs.Methods()){
  void*method=context.original_leaf(scope,oracle.strong_global_method);
  if(!method||context.env->ExceptionCheck())return false;
  request.owned.push_back(method);
 }
 for(const auto&oracle:inputs.Fields()){
  void*mirror=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,oracle.strong_global_field,&mirror))return false;
  void*field=request.field_leaf(mirror);if(!field)return false;
  request.owned.push_back(field);
 }
 void*foreign=context.original_leaf(scope,request.boot_method);
 if(!foreign||context.env->ExceptionCheck())return false;
 request.foreign.push_back(foreign);return true;
}
bool SourceCalibration::ProveAllocator(const exclusive_owned_roots::Context&context,void*raw){
 auto&request=*static_cast<Allocator*>(raw);auto&owner=request.owner;
 return retirement::CalibrateAllocatorMembership(owner.hooks_,owner.metadata_->Runtime().receivers.class_linker,
  context,request.owned,request.foreign,&owner.membership_,request.error);
}
bool SourceCalibration::PrepareRetirement(JNIEnv*env,jvmtiEnv*ti,std::string*error){
 if(membership_.matched)return true;
 const auto&inputs=metadata_->Inputs();
 if(!env||!ti||env->ExceptionCheck()||!exclusive_bound_||
    !BorrowFinalDexRecords(static_cast<JavaVM*>(metadata_->Runtime().receivers.java_vm)))
  return Fail(error,"source-allocator-needs-ordinary-JNI-and-actual-final-DEX-lease");
 struct Locals {JNIEnv*env;jobject loader=nullptr,storage_loader=nullptr,boot=nullptr,method=nullptr;
  ~Locals(){for(auto value:{method,boot,storage_loader,loader})if(value)env->DeleteLocalRef(value);}}local{env};
 struct Globals {JNIEnv*env;jobject loader=nullptr,boot=nullptr,method=nullptr;
  ~Globals(){for(auto value:{method,boot,loader})if(value)env->DeleteGlobalRef(value);}}global{env};
 if(ti->GetClassLoader(inputs.Anchor(),&local.loader)!=JVMTI_ERROR_NONE||!local.loader||env->ExceptionCheck()||
    ti->GetClassLoader(inputs.StorageClass(),&local.storage_loader)!=JVMTI_ERROR_NONE||
    !local.storage_loader||env->ExceptionCheck()||!env->IsSameObject(local.loader,local.storage_loader)||env->ExceptionCheck())
  return Fail(error,"source-allocator-complete-fixtures-do-not-share-the-owned-loader");
 global.loader=env->NewGlobalRef(local.loader);if(!global.loader||env->ExceptionCheck())return false;
 local.boot=env->FindClass("java/lang/Object");if(!local.boot||env->ExceptionCheck())return false;
 auto method=env->GetMethodID(static_cast<jclass>(local.boot),"toString","()Ljava/lang/String;");
 if(!method||env->ExceptionCheck())return false;
 local.method=env->ToReflectedMethod(static_cast<jclass>(local.boot),method,JNI_FALSE);
 if(!local.method||env->ExceptionCheck())return false;
 global.boot=env->NewGlobalRef(local.boot);if(!global.boot||env->ExceptionCheck())return false;
 global.method=env->NewGlobalRef(local.method);if(!global.method||env->ExceptionCheck())return false;
 auto field_leaf=reinterpret_cast<field_mirror::NamedField>(hooks_.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!field_leaf)return Fail(error,"source-allocator-original-field-leaf-unavailable");
 Allocator request{*this,global.method,field_leaf,{},{},error};
 request.owned.reserve(inputs.Methods().size()+inputs.Fields().size());request.foreign.reserve(1);
 if(runnable_executor::WithOriginalScope(env,inputs.Anchor(),CollectAllocator,&request)!=runnable_jni::Result::kSuccess||env->ExceptionCheck())
  return Fail(error,"source-allocator-complete-native-member-oracles-unavailable");
 // Native metadata identities remain owned by the full Class/mirror globals;
 // no borrowed movable Class, JNI Context or Thread crosses this stop.
 const jobject roots[]{global.loader,inputs.Anchor(),inputs.StorageClass(),global.boot,global.method};
 return exclusive_.With(env,roots,5,ProveAllocator,&request,error)&&membership_.matched;
}
void*SourceCalibration::ReferenceCurrent(void*raw){
 auto&owner=*static_cast<SourceCalibration*>(raw);
 return owner.reference_current_?owner.reference_current_():nullptr;
}
bool SourceCalibration::ReferenceOracle(void*raw,const runnable_jni::Context&context,jobject root,void**out){
 auto&owner=*static_cast<SourceCalibration*>(raw);
 return context.vm==owner.metadata_->Runtime().receivers.java_vm&&
  redefinition_gate::Owned(static_cast<JavaVM*>(context.vm))&&
  runnable_jni::Bridge::BorrowOriginalScope(context)&&owner.schema_.Ready()&&
  owner.schema_.Roots().BorrowOwnedStrongGlobal(context,root,out);
}
bool SourceCalibration::ReferenceScope(void*raw,JNIEnv*env,jobject root,runnable_jni::Operation operation,void*data){
 auto&owner=*static_cast<SourceCalibration*>(raw);JavaVM*vm=nullptr;
 if(!env||!root||!operation||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||
    vm!=owner.metadata_->Runtime().receivers.java_vm||!redefinition_gate::Owned(vm)||!owner.schema_.Ready())return false;
 return runnable_executor::WithOriginalScope(env,root,operation,data)==runnable_jni::Result::kSuccess&&
  !env->ExceptionCheck();
}
bool SourceCalibration::PrepareReferences(JNIEnv*env,std::string*error){
 const auto&runtime=metadata_->Runtime();JavaVM*vm=nullptr;
 if(!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||vm!=runtime.receivers.java_vm||
    !redefinition_gate::Owned(vm)||!schema_.Ready()||!schema_.Roots().Calibrated())
  return Fail(error,"source-reference-decoder-owner-or-original-root-calibration-unavailable");
 // Only a named, independently calibrated Current and actual Native caller
 // may enter preparation. No Thread/Context is retained across its GC requests.
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks_.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 void*self=current?current():nullptr;size_t extent=0;uint32_t state=UINT32_MAX;
 if(!self||!method_identity::ReadableMappingExtent(self,&extent)||!state_.State(self,extent,&state)||
    state!=abi::arm64::android11::kNative||current()!=self||(references_bound_&&reference_current_!=current))
  return Fail(error,"source-reference-decoder-preparation-needs-actual-Native-thread");
 if(!references_bound_){
  reference_current_=current;
  if(!references_.Bind(hooks_,{this,vm,ReferenceCurrent,ReferenceOracle,ReferenceScope},error))return false;
  references_bound_=true;
 }
 // Decoder owns and registers temporary strong/weak copies of these distinct
 // public globals. Only its actual cleared-weak GC oracle can make it ready.
 return references_.Calibrate(env,metadata_->Inputs().Anchor(),metadata_->Inputs().Storage(),error);
}
struct SourceCalibration::Interpretation {SourceCalibration&owner;std::string*error;};
bool SourceCalibration::PrepareInterpretation(JNIEnv*env,std::string*error){
 if(interpretation_ready_)return true;
 if(interpretation_attempted_)return Fail(error,"source-interpretation-prior-bootstrap-refused-with-process-hooks-retained");
 if(!interpretation_bound_||!membership_.matched||!schema_.Ready()||!stop_.Calibrated()||
    !env||env->ExceptionCheck()||!BorrowFinalDexRecords(static_cast<JavaVM*>(metadata_->Runtime().receivers.java_vm)))
  return Fail(error,"source-interpretation-bootstrap-dependencies-or-final-DEX-lease-unavailable");
 Interpretation request{*this,error};
 // The callback is the only path constructing the temporary methods/root
 // cohort. A failed attempt retains the same hook/registry owners; never
 // construct another Registry or repeat partially installed initialization.
 const auto result=runnable_executor::WithOriginalScope(env,metadata_->Inputs().Anchor(),ObserveInterpretation,&request);
 if(result!=runnable_jni::Result::kSuccess||env->ExceptionCheck()||!interpretation_ready_)
  return error&&!error->empty()?false:Fail(error,"source-interpretation-original-bootstrap-did-not-complete");
 return true;
}
bool SourceCalibration::ObserveInterpretation(const runnable_jni::Context&context,void*raw){
 auto&request=*static_cast<Interpretation*>(raw);auto&owner=request.owner;auto*error=request.error;
 // This is the sole preparation callback; no Context copy escapes.
 if(owner.interpretation_attempted_||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    context.vm!=owner.metadata_->Runtime().receivers.java_vm||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm)))
  return Fail(error,"source-interpretation-exact-original-context-unavailable");
 const auto&inputs=owner.metadata_->Inputs();
 const auto oracle=[&](const char*name)->const method_identity::Oracle*{
  const method_identity::Oracle*result=nullptr;
  for(const auto&member:inputs.MethodsSchema().methods)if(member.name==name&&member.descriptor=="()I"){
   for(const auto&candidate:inputs.Methods())if(candidate.dex_method_index==member.dex_index){
    if(result||(candidate.java_modifiers&0x500u))return nullptr;result=&candidate;
   }
  }
  return result;
 };
 const auto*positive=oracle("compute");const auto*negative=oracle("staticInvokeCalibration");
 size_t extent=0;
 if(!positive||!negative||positive==negative||!method_identity::ReadableMappingExtent(context.self,&extent))
  return Fail(error,"source-interpretation-independent-managed-method-oracles-unavailable");
 auto retired=owner.Retired();owner.interpretation_attempted_=true;
 owner.interpretation_ready_=owner.interpretation_.Initialize(owner.hooks_,context,
  owner.metadata_->Methods(),owner.metadata_->Fields(),owner.schema_.Roots(),inputs.Anchor(),*positive,*negative,
  owner.metadata_->Runtime(),owner.compilation_,owner.stop_,extent,retired,error);
 return owner.interpretation_ready_;
}
void SourceCalibration::Begin(void*driver,const runnable_jni::Context&context,void*raw)noexcept{
 auto&round=*static_cast<Round*>(raw);
 try{round.started=round.owner.schema_.Begin(driver,context,round.owner.metadata_->Inputs().OrdinaryClass(),&round.diagnostic);}
 catch(...){round.started=false;}
}
void SourceCalibration::End(void*driver,bool success,void*raw)noexcept{
 auto&round=*static_cast<Round*>(raw);if(!round.started)return;
 try{round.owner.schema_.End(driver,success,&round.diagnostic);}catch(...){}
 round.started=false;
}
}

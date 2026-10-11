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
  if(!process->Install(env,error)||!process->Calibrate(env,ti,error)||!process->PrepareCommitReaders(env,error)||!process->PrepareTypedHeap(env,error))return nullptr;
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
 if(!compilation_bound_){if(!compilation_.Bind(hooks_,runtime,error))return false;compilation_bound_=true;}
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

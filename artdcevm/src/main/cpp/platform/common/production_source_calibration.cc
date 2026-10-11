#include "platform/common/production_source_calibration.h"
#include "platform/common/production_final_dex.h"
#include "platform/common/thread_state_calibration.h"
#include "platform/common/native_typed_heap_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/native_call_graph_incoming.h"
#include "platform/common/production_receiver_invocation.h"
#include "platform/common/production_final_write_calibration.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/modern_method_guards.h"
#include <cstring>
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
  if(!process->Install(env,error)||!process->Calibrate(env,ti,error)||!process->PrepareCommitReaders(env,error)||!process->PrepareTypedHeap(env,error)||!process->PrepareRetirement(env,ti,error)||!process->PrepareMethodEntries(env,error)||!process->PrepareFieldEntries(env,error)||!process->PrepareInterpretation(env,error)||!process->PrepareReferences(env,error)||!process->PrepareStack(env,error)||!process->PrepareReceiverCode(env,error)||!process->PrepareReceiverEvents(env,error)||!CalibrateReceiverInvocations(hooks,env,*process->metadata_,process->schema_.Roots(),process->stop_,process->references_,*process->receiver_calibration_,error))return nullptr;
  if(!process->final_writer_calibrated_){
   if(!CalibrateFinalWrites(hooks,env,*process->metadata_,process->schema_.Roots(),process->stop_,
       process->final_writer_->Writer(),error))return nullptr;
   process->final_writer_calibrated_=true;
  }
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
struct SourceCalibration::ReceiverEvents {SourceCalibration&owner;std::string*error;bool completed=false;};
bool SourceCalibration::PrepareMethodEntries(JNIEnv*env,std::string*error){
 const auto&inputs=metadata_->Inputs();const auto manager=metadata_->Runtime().receivers.jni_id_manager;
 if(!membership_.matched||!retirement::InstallMethodEntryGuards(hooks_,env,inputs.Anchor(),manager,retired_methods_,error)||
    !retirement::MethodEntryGuardsOwn(retired_methods_,manager))return false;
 const char*names[]{"compute","privateAccessCalibration","staticInvokeCalibration"};
 std::vector<retirement::MethodHandleOracle>oracles;oracles.reserve(inputs.MethodHandles().size());
 for(size_t n=0;n<inputs.MethodHandles().size();++n){
  const evolution::Member*declaration=nullptr;
  for(const auto&method:inputs.MethodsSchema().methods)
   if(method.name==names[n]&&method.descriptor=="()I"){
    if(declaration)return Fail(error,"source-method-handle-independent-declaration-ambiguous");
    declaration=&method;
   }
  jobject mirror=nullptr;
  if(declaration)for(const auto&method:inputs.Methods())
   if(method.dex_method_index==declaration->dex_index&&!(method.java_modifiers&0x100u)&&
      bool(method.java_modifiers&8u)==(n==2)){
    if(mirror)return Fail(error,"source-method-handle-independent-mirror-ambiguous");
    mirror=method.strong_global_method;
   }
  if(!mirror||!inputs.MethodHandles()[n])return Fail(error,"source-method-handle-independent-input-unavailable");
  auto id=env->FromReflectedMethod(mirror);
  if(!id||env->ExceptionCheck())return false;
  oracles.push_back({inputs.MethodHandles()[n],id});
 }
 if(!retirement::PrepareMethodHandleGuards(hooks_,env,manager,retired_methods_,oracles,error))return false;
 for(size_t n=0;n<oracles.size();++n){
  if(!retirement::BeginMethodHandleTargetProof(env,oracles[n].strong_global,oracles[n].method,error))return false;
  bool succeeded=false;
  try{succeeded=inputs.ExerciseMethodHandle(env,n,error);}
  catch(...){retirement::EndMethodHandleTargetProof(false,nullptr);throw;}
  if(!retirement::EndMethodHandleTargetProof(succeeded&&!env->ExceptionCheck(),error))return false;
 }
 return retirement::MethodHandleGuardsReady(retired_methods_)||
  Fail(error,"source-method-handle-original-outer-invocation-not-observed");
}
bool SourceCalibration::PrepareFieldEntries(JNIEnv*env,std::string*error){
 if(!membership_.matched||!metadata_->Fields().Calibrated())
  return Fail(error,"source-field-entry-independent-metadata-or-retirement-owner-unavailable");
 if(!retirement::InstallFieldEntryGuards(hooks_,env,metadata_->Inputs().Anchor(),
  metadata_->Runtime().receivers.jni_id_manager,metadata_->FieldMirrors(),retired_fields_,error)||
  !retirement::FieldEntryGuardsOwn(retired_fields_))return false;
 return PrepareFieldVarHandles(env,error);
}
struct SourceCalibration::FieldHandles {
 SourceCalibration&owner;field_mirror::NamedField leaf;
 std::array<jobject,2>fields{};
 std::vector<retirement::FieldVarHandleOracle>oracles;
 std::string*error;bool completed=false;
};
bool SourceCalibration::ObserveFieldHandles(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<FieldHandles*>(raw);const auto&inputs=r.owner.metadata_->Inputs();
 if(!actual.env||actual.env->ExceptionCheck()||!r.leaf||
    actual.vm!=r.owner.metadata_->Runtime().receivers.java_vm||
    !runnable_jni::Bridge::BorrowOriginalScope(actual)||!redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm)))
  return Fail(r.error,"source-field-VarHandle-original-context-unavailable");
 for(size_t n=0;n<r.fields.size();++n){
  void*mirror=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(actual,r.fields[n],&mirror)||!mirror)return false;
  void*field=r.leaf(mirror);
  if(!field||actual.env->ExceptionCheck())return false;
  r.oracles.push_back({inputs.FieldVarHandles()[n],field,r.fields[n]});
 }
 r.completed=r.oracles.size()==r.fields.size()&&runnable_jni::Bridge::BorrowOriginalScope(actual);
 return r.completed;
}
bool SourceCalibration::PrepareFieldVarHandles(JNIEnv*env,std::string*error){
 const auto&inputs=metadata_->Inputs();
 auto leaf=reinterpret_cast<field_mirror::NamedField>(hooks_.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!leaf)return Fail(error,"source-field-VarHandle-named-field-reader-unavailable");
 FieldHandles request{*this,leaf,{}, {},error};request.oracles.reserve(request.fields.size());
 const char*names[]{"intValue","staticIntValue"};
 for(size_t n=0;n<request.fields.size();++n){
  for(const auto&field:inputs.Fields())
   if(field.name&&field.descriptor&&std::strcmp(field.name,names[n])==0&&std::strcmp(field.descriptor,"I")==0&&
      bool(field.modifiers&8u)==bool(n)){
    if(request.fields[n])return Fail(error,"source-field-VarHandle-independent-field-ambiguous");
    request.fields[n]=field.strong_global_field;
   }
  if(!request.fields[n]||!inputs.FieldVarHandles()[n])
   return Fail(error,"source-field-VarHandle-independent-input-unavailable");
 }
 if(runnable_executor::WithOriginalScope(env,inputs.Anchor(),ObserveFieldHandles,&request)!=runnable_jni::Result::kSuccess||
    !request.completed||env->ExceptionCheck())return Fail(error,"source-field-VarHandle-original-target-collection-refused");
 if(!retirement::InstallFieldVarHandleGuards(hooks_,env,request.oracles,retired_fields_,error)||
    !inputs.ExerciseFieldHandles(env,error))return false;
 if(!retirement::FieldVarHandleGuardsReady(retired_fields_))
  return Fail(error,"source-field-VarHandle-original-Access-targets-not-observed");
 return PrepareFieldMethodHandles(env,request.fields,error);
}
bool SourceCalibration::PrepareFieldMethodHandles(JNIEnv*env,const std::array<jobject,2>&fields,std::string*error){
 const auto&inputs=metadata_->Inputs();
 struct Identities {
  JNIEnv*env;std::array<std::shared_ptr<contextual_field_id::Proof>,2>proofs{};
  ~Identities(){for(auto&proof:proofs)contextual_field_id::Release(env,&proof);}
 }identities{env};
 std::array<jfieldID,2>ids{};
 for(size_t n=0;n<fields.size();++n){
  ids[n]=env->FromReflectedField(fields[n]);
  if(!ids[n]||env->ExceptionCheck())return false;
  identities.proofs[n]=contextual_field_id::Prepare(env,fields[n],ids[n],error);
  if(!identities.proofs[n]||contextual_field_id::IsStatic(identities.proofs[n])!=bool(n))return false;
 }
 std::vector<retirement::FieldMethodHandleOracle>oracles;oracles.reserve(inputs.FieldMethodHandles().size());
 for(size_t n=0;n<inputs.FieldMethodHandles().size();++n){const size_t field=n/2;
  oracles.push_back({inputs.FieldMethodHandles()[n],ids[field],identities.proofs[field],uint32_t(8+n)});
 }
 if(!retirement::InstallMethodHandleFieldGuards(hooks_,error)||
    !retirement::PrepareMethodHandleFieldGuards(hooks_,env,metadata_->Runtime().receivers.jni_id_manager,
     retired_fields_,oracles,error))return false;
 for(size_t n=0;n<oracles.size();++n){
  if(!retirement::BeginMethodHandleFieldTargetProof(env,oracles[n].strong_global,oracles[n].field,error))return false;
  bool succeeded=false;
  try{succeeded=inputs.ExerciseFieldMethodHandle(env,n,error);}
  catch(...){retirement::EndMethodHandleFieldTargetProof(false,nullptr);throw;}
  if(!retirement::EndMethodHandleFieldTargetProof(succeeded&&!env->ExceptionCheck(),error))return false;
 }
 return retirement::MethodHandleFieldGuardsReady(retired_fields_)||
  Fail(error,"source-field-MethodHandle-four-original-kinds-not-observed");
}
bool SourceCalibration::InstallReceiverEvents(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<ReceiverEvents*>(raw);auto&owner=r.owner;size_t extent=0;
 if(!runnable_jni::Bridge::BorrowOriginalScope(actual)||!actual.env||actual.env->ExceptionCheck()||
    actual.vm!=owner.metadata_->Runtime().receivers.java_vm||!redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm))||
    !owner.receiver_listener_bound_||!method_identity::ReadableMappingExtent(actual.self,&extent))
  return Fail(r.error,"source-receiver-listener-original-installation-context-unavailable");
 struct Install {ReceiverEvents&request;void*self;}request{r,actual.self};
 native_source_stop::Session stop(owner.stop_);
 const bool inspected=stop.Inspect(actual.self,extent,&request,[](void*raw,native_source_stop::Session&session)noexcept{
  auto&r=*static_cast<Install*>(raw);
  try{return r.request.owner.receiver_listener_->Install(session,r.self,r.request.error);}
  catch(...){return false;} // Report only after the actual stop has resumed.
 });
 if(stop.Reserved()||stop.IsStopped(actual.self))std::abort();
 r.completed=inspected&&owner.receiver_listener_->Installed()&&
  runnable_jni::Bridge::BorrowOriginalScope(actual)&&!actual.env->ExceptionCheck();
 return r.completed;
}
bool SourceCalibration::PrepareReceiverEvents(JNIEnv*env,std::string*error){
 if(!receiver_code_calibrated_||!references_.Ready()||!stop_.Calibrated())
  return Fail(error,"source-receiver-listener-code-or-original-stop-dependencies-unavailable");
 if(!pending_exception_.Calibrated())return Fail(error,"source-receiver-listener-native-pending-reader-unavailable");
 if(!final_writer_){
  final_writer_=final_write_guard::NativeAccess::Create(hooks_,metadata_->Fields(),metadata_->Methods(),references_,error);
  if(!final_writer_)return false;
 }
 if(!receiver_frames_)receiver_frames_=std::make_unique<receiver_guard::FrameReader>(receiver_producer_.Frames());
 if(!receiver_bytecode_)receiver_bytecode_=std::make_unique<receiver_guard::BytecodeSource>(*receiver_frames_,receiver_producer_.Access());
 if(!receiver_calibration_)receiver_calibration_=std::make_unique<receiver_guard::SourceCalibration>(*receiver_frames_,*receiver_bytecode_,references_);
 if(!receiver_listener_)receiver_listener_=std::make_unique<receiver_guard::ListenerOwner>(receiver_calibration_->Observer());
 if(!receiver_listener_bound_){
  if(!receiver_listener_->Bind(hooks_,metadata_->Runtime(),stack_declaration_,metadata_->Methods(),
     retired_fields_,pending_exception_,final_writer_->Writer(),error))return false;
  receiver_listener_bound_=true;
 }
 if(receiver_listener_->Installed())return true;
 ReceiverEvents request{*this,error};
 const auto result=runnable_executor::WithOriginalScope(env,metadata_->Inputs().Anchor(),InstallReceiverEvents,&request);
 if(result!=runnable_jni::Result::kSuccess||!request.completed||env->ExceptionCheck())
  return error&&!error->empty()?false:Fail(error,"source-receiver-listener-original-installation-did-not-complete");
 return true;
}
struct SourceCalibration::ReceiverCode {SourceCalibration&owner;std::string*error;bool completed=false;};
bool SourceCalibration::ObserveReceiverCode(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<ReceiverCode*>(raw);auto&owner=r.owner;
 if(!runnable_jni::Bridge::BorrowOriginalScope(actual)||!actual.original_leaf||!actual.env||
    actual.env->ExceptionCheck()||actual.vm!=owner.metadata_->Runtime().receivers.java_vm||
    !redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm))||!owner.receiver_producer_.OwnsCurrentThread(actual.self))
  return Fail(r.error,"source-receiver-code-original-context-unavailable");
 const auto&inputs=owner.metadata_->Inputs();
 const auto layout=owner.metadata_->Methods().model==method_identity::Model::kMethod11?
  method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 const abi::common::RunnableJniScope scope{actual.self,actual.env,actual.vm};
 const auto identity=[&](const method_identity::Oracle&oracle,jclass klass,const std::string&descriptor,void**out){
  void*method=actual.original_leaf(scope,oracle.strong_global_method);size_t extent=0;
  method_metadata::Identity observed{};
  if(!method||actual.env->ExceptionCheck()||!method_identity::ReadableMappingExtent(method,&extent)||
     !method_metadata::ReadIdentity(layout,method,extent,&observed)||
     observed.dex_method_index!=oracle.dex_method_index||(observed.access_flags&0x1fffu)!=(oracle.java_modifiers&0x1fffu))return false;
  void*root=nullptr;
  if(!owner.schema_.Roots().BorrowOwnedStrongGlobal(actual,klass,&root)||
     owner.metadata_->Fields().MethodDeclaringClass(method)!=root)return false;
  field_copy::CopyClassView view;
  if(!owner.metadata_->Fields().ClassIdentity(root,&view,nullptr)||!view.descriptor||descriptor!=view.descriptor)return false;
  *out=method;return owner.receiver_producer_.OwnsCurrentThread(actual.self);
 };
 for(const auto&sample:inputs.ReceiverSamples()){
  void*caller=nullptr,*target=nullptr;
  if(!identity(sample.caller,sample.caller_class,sample.caller_descriptor,&caller)||
     !identity(sample.target,sample.target_class,sample.target_descriptor,&target))
   return Fail(r.error,"source-receiver-code-independent-method-or-Class-identity-mismatch");
  native_call_graph_incoming::Inventory inventory;
  if(!inventory.Capture(inputs.ReceiverDex(),sample.caller_descriptor,r.error))return false;
  receiver_guard::CodeView code;
  if(!owner.receiver_producer_.Code(actual.self,caller,&code)||
     !inventory.MatchesCode(sample.caller.dex_method_index,code)||
     !code.instructions||code.units!=sample.instructions.size()/2||sample.instructions.size()%2||
     std::memcmp(code.instructions,sample.instructions.data(),sample.instructions.size()))
   return Fail(r.error,"source-receiver-code-original-CodeItem-differs-from-independent-DEX-and-JVMTI");
 }
 r.completed=owner.receiver_producer_.OwnsCurrentThread(actual.self)&&
  runnable_jni::Bridge::BorrowOriginalScope(actual)&&!actual.env->ExceptionCheck();
 return r.completed;
}
bool SourceCalibration::PrepareReceiverCode(JNIEnv*env,std::string*error){
 if(!stack_calibrated_||!references_.Ready()||!reference_current_||!schema_.Ready())
  return Fail(error,"source-receiver-code-constructor-or-root-dependencies-unavailable");
 if(!pending_exception_.Calibrate(hooks_,env,static_cast<JavaVM*>(metadata_->Runtime().receivers.java_vm),
    metadata_->Storage(),schema_.Roots(),error))return false;
 if(!receiver_bound_){
  if(!receiver_producer_.Bind(hooks_,stack_declaration_,metadata_->Methods(),metadata_->Fields(),
       metadata_->Runtime().receivers.class_linker,reference_current_,error,
       &pending_exception_))return false;
  receiver_bound_=true;
 }
 receiver_code_calibrated_=false;ReceiverCode request{*this,error};
 const auto result=runnable_executor::WithOriginalScope(env,metadata_->Inputs().Anchor(),ObserveReceiverCode,&request);
 receiver_code_calibrated_=result==runnable_jni::Result::kSuccess&&request.completed&&!env->ExceptionCheck();
 if(!receiver_code_calibrated_&&error&&error->empty())*error="source-receiver-code-original-samples-did-not-complete";
 return receiver_code_calibrated_;
}
bool SourceCalibration::PrepareStack(JNIEnv*env,std::string*error){
 stack_visitor_service::Declaration observed;
 if(!stack_visitor_calibration::Calibrate(hooks_,env,metadata_->Inputs().Anchor(),metadata_->Methods(),&observed,error))return false;
 if(stack_calibrated_&&observed!=stack_declaration_)return Fail(error,"source-stack-constructor-declaration-changed");
 stack_declaration_=observed;stack_calibrated_=true;return true;
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

#include "platform/common/capability_backend.h"
#include "support/json.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/body_only_policy.h"
namespace artdcevm {
CapabilityBackend::CapabilityBackend(std::unique_ptr<RuntimeBackend> public_backend,Producer producer):public_(std::move(public_backend)),producer_(std::move(producer)){}
int CapabilityBackend::Attach(JavaVM* vm){
 OperationScope operation(operation_);if(!operation)return JNI_ERR;
 if(vm_)return JNI_OK;
 if(!vm||!public_||public_->Attach(vm)!=JNI_OK)return JNI_ERR;
 jvmtiEnv* ti=nullptr;
 if(vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK||!ti)return JNI_ERR;
 jvmtiCapabilities potential{};
 if(ti->GetPotentialCapabilities(&potential)!=JVMTI_ERROR_NONE){ti->DisposeEnvironment();return JNI_ERR;}
 jvmtiCapabilities requested{};requested.can_redefine_classes=potential.can_redefine_classes;
 if(ti->AddCapabilities(&requested)!=JVMTI_ERROR_NONE){ti->DisposeEnvironment();return JNI_ERR;}
 vm_=vm;env_=ti;return JNI_OK;
}
int CapabilityBackend::Prepare(JNIEnv* env,jobject calibration){
 if(common_)return JVMTI_ERROR_NONE;
 if(!env_||!env||!calibration)return JVMTI_ERROR_INVALID_ENVIRONMENT;
 if(attempted_)return JVMTI_ERROR_NOT_AVAILABLE;
 attempted_=true;
 common_=producer_?producer_(env,env_,calibration,&error_):nullptr;
 if(!common_&&error_.empty())error_="No independently proven production bootstrap";
 return common_?JVMTI_ERROR_NONE:JVMTI_ERROR_NOT_AVAILABLE;
}
int CapabilityBackend::InitializeEvolution(JNIEnv* env,jobject calibration){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 const int result=Prepare(env,calibration);if(result!=JVMTI_ERROR_NONE)return result;
 // Common activation proves individual capabilities, never the complete
 // evolution contract. Do not install a second owner or activate a subset on
 // behalf of a caller that explicitly requested full initialization.
 error_="Full evolution unavailable: complete schema, hierarchy, active-frame and entry-path behavior contract has not been independently proven; use initializeCapabilities for individually proven common capabilities";
 return JVMTI_ERROR_NOT_AVAILABLE;
}
int CapabilityBackend::ActivateCommon(JNIEnv* env,jobject calibration){
 if(ready_)return JVMTI_ERROR_NONE;
 const int result=Prepare(env,calibration);if(result!=JVMTI_ERROR_NONE)return result;
 if(!common_->Activate(env,&error_))return JVMTI_ERROR_NOT_AVAILABLE;
 ready_=true;error_.clear();return JVMTI_ERROR_NONE;
}
int CapabilityBackend::InitializeCapabilities(JNIEnv* env,jobject calibration){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 return ActivateCommon(env,calibration);
}
int CapabilityBackend::InitializeCommonCapabilities(JNIEnv* env,jobject calibration){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 return ActivateCommon(env,calibration);
}
std::string CapabilityBackend::Capabilities()const {
 OperationScope operation(operation_);
 if(!operation)return "{\"backend\":\"runtime-capabilities\",\"operationBusy\":true,\"evolutionError\":\"Runtime backend operation already in progress\"}";
 const std::string source_diagnostics=std::string(",\"sourceCapabilitiesCalibrated\":")+
   (ready_&&common_&&common_->SourceCapabilitiesCalibrated()?"true":"false")+
   ",\"sourceFactoryDiagnostic\":"+QuoteJsonMutf8(common_?common_->SourceFactoryDiagnostic():"production-source-owner-not-observed")+
   ",\"initializerEpisodeObserved\":"+(common_&&common_->InitializerEpisodeObserved()?"true":"false")+
   ",\"initializerEpisodeDiagnostic\":"+QuoteJsonMutf8(common_?common_->InitializerEpisodeDiagnostic():"initializer-source-owner-not-observed")+
   ",\"receiverInvokeSourceCalibrated\":"+(common_&&common_->ReceiverInvokeSourceCalibrated()?"true":"false")+
   ",\"receiverInvokeSourceDiagnostic\":"+QuoteJsonMutf8(common_?common_->ReceiverInvokeSourceDiagnostic():"receiver-invoke-source-owner-not-observed")+
   ",\"receiverEntrySourceCalibrated\":"+(common_&&common_->ReceiverEntrySourceCalibrated()?"true":"false")+
   ",\"receiverEntrySourceDiagnostic\":"+QuoteJsonMutf8(common_?common_->ReceiverEntrySourceDiagnostic():"receiver-entry-source-owner-not-observed")+
   ",\"receiverHandleSourceCalibrated\":"+(common_&&common_->ReceiverHandleSourceCalibrated()?"true":"false")+
   ",\"receiverHandleSourceDiagnostic\":"+QuoteJsonMutf8(common_?common_->ReceiverHandleSourceDiagnostic():"receiver-handle-source-owner-not-observed")+
   ",\"receiverOriginalEntriesObserved\":"+(common_&&common_->ReceiverOriginalEntriesObserved()?"true":"false")+
   ",\"receiverOriginalEntriesDiagnostic\":"+QuoteJsonMutf8(common_?common_->ReceiverOriginalEntriesDiagnostic():"receiver-original-entries-owner-not-observed")+
   ",\"rootProvenanceObserved\":"+(common_&&common_->RootProvenanceObserved()?"true":"false")+
   ",\"rootProvenanceDiagnostic\":"+QuoteJsonMutf8(common_?common_->RootProvenanceDiagnostic():"runtime-root-observation-owner-not-observed")+
   ",\"rootProvenanceReport\":"+(common_?common_->RootProvenanceReport():"{}")+
   ",\"typedHeapCalibrated\":"+(common_&&common_->TypedHeapCalibrated()?"true":"false")+
   ",\"typedHeapDiagnostic\":"+QuoteJsonMutf8(common_?common_->TypedHeapDiagnostic():"typed-heap-source-owner-not-observed");
 const std::string public_report=public_->Capabilities();
 auto supported=[&](const char* key){return public_report.find(std::string("\"")+key+"\":true")!=std::string::npos;};
 std::string result="{\"backend\":\"runtime-capabilities\",\"attached\":"+std::string(vm_?"true":"false")+
   ",\"methodBody\":"+(supported("methodBody")?"true":"false")+",\"nativeAdditive\":"+(supported("nativeAdditive")?"true":"false")+
   ",\"partialEvolutionAvailable\":"+(ready_?"true":"false")+",\"atomicMemberBatch\":"+(ready_&&common_&&common_->AtomicBatchReady()?"true":"false")+",\"nativeMethodDeletion\":"+(ready_?"true":"false")+
   ",\"nativeDelete\":"+std::string(ready_&&common_&&common_->FieldDeletionReady()?"true":"false")+",\"evolutionAvailable\":false,\"fieldDeletion\":"+(ready_&&common_&&common_->FieldDeletionReady()?"true":"false")+",\"standardRedefineRouted\":false,\"fullDcevmComplete\":false,\"validationHooksInstalled\":false,\"privateHooksSupported\":"+(ready_?"true":"false")+
   ",\"sourceStopCalibrated\":"+(ready_&&common_&&common_->SourceStopCalibrated()?"true":"false")+
   ",\"nativeScopeCalibrated\":"+(ready_&&common_&&common_->NativeScopeCalibrated()?"true":"false")+
   ",\"sourceStopDiagnostic\":"+QuoteJsonMutf8(common_?common_->SourceStopDiagnostic():"ordinary-source-lifecycle-not-observed")+
   source_diagnostics+",\"evolutionError\":"+QuoteJsonMutf8(error_)+",\"public\":"+public_report+"}";
 return result;
}
int CapabilityBackend::Apply(Operation op,const std::vector<Definition>& definitions){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 if(op!=Operation::kEvolve){
  int result=public_->Apply(op,definitions);
  if(result==JVMTI_ERROR_NONE&&common_){
   JNIEnv* env=nullptr;if(vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK&&env)common_->RecordDefinitions(env,definitions);
  }
  return result;
 }
 // A declaration-equivalent evolution request uses the already attached
 // public JVMTI body route. ART still validates code/annotations and applies
 // ordinary constructor/clinit/native semantics; schema readiness grants none
 // of those effects. Every class in a batch must independently match.
 if(!definitions.empty()&&env_&&vm_){
  JNIEnv* ordinary=nullptr;
  if(vm_->GetEnv(reinterpret_cast<void**>(&ordinary),JNI_VERSION_1_6)==JNI_OK&&ordinary&&!ordinary->ExceptionCheck()){
   bool body=true;
   for(const auto& d:definitions){evolution::ClassSchema live,dex;std::string diagnostic;
    if(live_schema::ReadLiveSchema(env_,ordinary,d.klass,&live,&diagnostic)!=JVMTI_ERROR_NONE||
       ordinary->ExceptionCheck()||!evolution::ReadDexDeclarations(d.dex,&dex,&diagnostic)||
       !body_only::SameDeclarations(live,dex)){body=false;break;}
   }
   if(ordinary->ExceptionCheck())return JVMTI_ERROR_INTERNAL;
   if(body){const int result=public_->Apply(Operation::kBody,definitions);
    if(result==JVMTI_ERROR_NONE&&common_)common_->RecordDefinitions(ordinary,definitions);
    return result;
   }
  }
 }
 if(!ready_||!common_)return JVMTI_ERROR_NOT_AVAILABLE;
 JNIEnv* env=nullptr;
 if(vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)return JVMTI_ERROR_INVALID_ENVIRONMENT;
 return common_->Apply(env,env_,definitions,&error_);
}
std::string CapabilityBackend::Analyze(JNIEnv* env,const std::vector<Definition>& defs){
 OperationScope operation(operation_);
 if(!operation)return "{\"operationBusy\":true,\"error\":\"Runtime backend operation already in progress\"}";
 return public_->Analyze(env,defs);
}
std::string CapabilityBackend::ErrorName(int code)const{
 OperationScope operation(operation_);
 return operation?public_->ErrorName(code):"Runtime backend operation already in progress";
}
int CapabilityBackend::EnableValidationHooks(){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 error_="Validation-first hook installation is not independently proven by the common owner";
 return JVMTI_ERROR_NOT_AVAILABLE;
}
int CapabilityBackend::EnableStandardRedefineRouting(){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 error_="Standard RedefineClasses routing is not independently proven by the common owner";
 return JVMTI_ERROR_NOT_AVAILABLE;
}
}

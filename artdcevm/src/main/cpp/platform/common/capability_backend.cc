#include "platform/common/capability_backend.h"
#include "support/json.h"
namespace artdcevm {
CapabilityBackend::CapabilityBackend(std::unique_ptr<RuntimeBackend> public_backend,SourceFactory source,Producer producer):public_(std::move(public_backend)),source_factory_(std::move(source)),producer_(std::move(producer)){}
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
int CapabilityBackend::PrepareSource(JNIEnv* env,jobject calibration){
 const int result=Prepare(env,calibration);if(result!=JVMTI_ERROR_NONE)return result;
 if(source_ready_)return JVMTI_ERROR_NONE;
 if(!common_->Source13Candidate()||!source_factory_){error_="Full source evolution is not proven; use initializeCapabilities for the proven subset";return JVMTI_ERROR_NOT_AVAILABLE;}
 if(source_attempted_){error_="Source initialization already failed; restart the process";return JVMTI_ERROR_NOT_AVAILABLE;}
 source_attempted_=true;
 if(!source_)source_=source_factory_();
 if(!source_||source_->Attach(vm_)!=JNI_OK){error_="Proven source backend could not attach";return JVMTI_ERROR_NOT_AVAILABLE;}
 const int initialized=source_->InitializeEvolution(env,calibration);
 if(initialized!=JVMTI_ERROR_NONE){error_="Proven source candidate failed full initialization; restart before retry";return initialized;}
 source_ready_=true;ready_=true;return JVMTI_ERROR_NONE;
}
int CapabilityBackend::InitializeEvolution(JNIEnv* env,jobject calibration){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 // Explicit full initialization never reports a subset as success.
 if(ready_&&!source_ready_){error_="Subset initialized; full evolution is not proven";return JVMTI_ERROR_NOT_AVAILABLE;}
 return PrepareSource(env,calibration);
}
int CapabilityBackend::InitializeCapabilities(JNIEnv* env,jobject calibration){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 if(ready_)return JVMTI_ERROR_NONE;
 const int result=Prepare(env,calibration);if(result!=JVMTI_ERROR_NONE)return result;
 if(common_->Source13Candidate())return PrepareSource(env,calibration);
 if(!common_->Activate(env,&error_))return JVMTI_ERROR_NOT_AVAILABLE;
 ready_=true;error_.clear();return JVMTI_ERROR_NONE;
}
std::string CapabilityBackend::Capabilities()const {
 OperationScope operation(operation_);
 if(!operation)return "{\"backend\":\"runtime-capabilities\",\"operationBusy\":true,\"evolutionError\":\"Runtime backend operation already in progress\"}";
 const std::string source_diagnostics=std::string(",\"sourceCapabilitiesCalibrated\":")+
   (ready_&&common_&&common_->SourceCapabilitiesCalibrated()?"true":"false")+
   ",\"frameGeometryCalibrated\":"+(ready_&&common_&&common_->FrameGeometryCalibrated()?"true":"false")+
   ",\"sourceFactoryDiagnostic\":"+QuoteJsonMutf8(common_?common_->SourceFactoryDiagnostic():"production-source-owner-not-observed");
 if(source_){
  auto report=source_->Capabilities();
  if(!report.empty()&&report.back()=='}'){report.pop_back();report+=source_diagnostics+"}";}
  return report;
 }
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
 if(source_ready_)return source_->Apply(op,definitions);
 if(!ready_||!common_)return JVMTI_ERROR_NOT_AVAILABLE;
 JNIEnv* env=nullptr;
 if(vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)return JVMTI_ERROR_INVALID_ENVIRONMENT;
 return common_->Apply(env,env_,definitions,&error_);
}
std::string CapabilityBackend::Analyze(JNIEnv* env,const std::vector<Definition>& defs){
 OperationScope operation(operation_);
 if(!operation)return "{\"operationBusy\":true,\"error\":\"Runtime backend operation already in progress\"}";
 return source_ready_?source_->Analyze(env,defs):public_->Analyze(env,defs);
}
std::string CapabilityBackend::ErrorName(int code)const{
 OperationScope operation(operation_);
 return operation?public_->ErrorName(code):"Runtime backend operation already in progress";
}
int CapabilityBackend::EnableValidationHooks(){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 if(source_)return source_->EnableValidationHooks();
 if(!vm_||ready_||(!common_&&attempted_))return JVMTI_ERROR_NOT_AVAILABLE;
 JNIEnv* env=nullptr;
 if(vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)return JVMTI_ERROR_INVALID_ENVIRONMENT;
 // The diagnostic validation-first API remains supported, but it obtains the
 // same complete production oracles before installing any private callback.
 if(!common_){
  jclass type=env->FindClass("com/github/ytlog/artdcevm/internal/EvolutionCalibration");
  jmethodID ctor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
  jobject calibration=ctor?env->NewObject(type,ctor):nullptr;
  const int result=calibration&&!env->ExceptionCheck()?Prepare(env,calibration):JVMTI_ERROR_NOT_AVAILABLE;
  if(calibration)env->DeleteLocalRef(calibration);if(type)env->DeleteLocalRef(type);
  if(result!=JVMTI_ERROR_NONE)return result;
 }
 if(!common_->Source13Candidate()||!source_factory_)return JVMTI_ERROR_NOT_AVAILABLE;
 source_=source_factory_();
 if(!source_||source_->Attach(vm_)!=JNI_OK)return JVMTI_ERROR_NOT_AVAILABLE;
 // These callbacks preserve original ART behavior until full source bootstrap
 // configures its actual transaction handler. No incomplete private evolution.
 return source_->EnableValidationHooks();
}
int CapabilityBackend::EnableStandardRedefineRouting(){
 OperationScope operation(operation_);if(!operation)return JVMTI_ERROR_NOT_AVAILABLE;
 return source_ready_?source_->EnableStandardRedefineRouting():JVMTI_ERROR_NONE;
}
}

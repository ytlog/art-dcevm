#include "platform/common/production_metadata_calibration.h"
#include "platform/common/owned_method_table.h"
#include "platform/common/production_final_dex.h"
#include "platform/common/runnable_executor.h"
namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
}
struct MetadataCalibration::Request {
 MetadataCalibration&owner;HookEngine&hooks;
 field_mirror::NamedField field_leaf;std::string*error;
};
std::unique_ptr<MetadataCalibration>MetadataCalibration::Create(HookEngine&hooks,JNIEnv*env,
 jvmtiEnv*ti,jobject fixture,std::string*error){
 auto result=std::unique_ptr<MetadataCalibration>(new MetadataCalibration);
 if(!result->Prepare(hooks,env,ti,fixture,error))return nullptr;
 return result;
}
bool MetadataCalibration::Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 if(!env||!ti||!fixture||env->ExceptionCheck())return Fail(error,"metadata-calibration-invalid-caller");
 if(!runtime_services::CalibrateRuntimeServices(hooks,env,&runtime_,error))return false;
 inputs_=CalibrationInputs::Create(env,ti,fixture,error);if(!inputs_)return false;
 auto*vm=static_cast<JavaVM*>(runtime_.receivers.java_vm);
 redefinition_gate::Lease lease(vm);
 if(!lease.Acquired()||!BorrowFinalDexRecords(vm))return Fail(error,"metadata-calibration-final-DEX-history-or-lease-unavailable");
 if(!runtime_.runtime||!runtime_.instrumentation||!runtime_.receivers.class_linker||!runtime_.receivers.heap)
  return Fail(error,"metadata-calibration-runtime-receivers-unavailable");
 if(!fields_.Bind(hooks,error))return false;
 auto field_leaf=reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!field_leaf)return Fail(error,"metadata-calibration-named-field-mirror-leaf-unavailable");
 Request request{*this,hooks,field_leaf,error};
 const auto observed=runnable_executor::WithOriginalScope(env,inputs_->Anchor(),Observe,&request);
 if(observed!=runnable_jni::Result::kSuccess||env->ExceptionCheck()){
  if(error&&error->empty())*error="metadata-calibration-original-JNI-scope-did-not-complete";
  return false;
 }
 // Storage/card/monitor calibration has its own ordinary Native entry. Never
 // nest this stop-capable operation in the retained original Runnable callback.
 return storage_.Calibrate(hooks,env,inputs_->StorageClass(),inputs_->Storage(),fields_,
  inputs_->Fields().data(),inputs_->Fields().size(),error);
}
bool MetadataCalibration::Observe(const runnable_jni::Context&context,void*raw){
 auto&request=*static_cast<Request*>(raw);auto&owner=request.owner;
 const auto&inputs=*owner.inputs_;const auto&methods=inputs.Methods();const auto&fields=inputs.Fields();
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||!context.original_leaf||
    context.vm!=owner.runtime_.receivers.java_vm||!methods.size()||!fields.size()||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm)))
  return Fail(request.error,"metadata-calibration-exact-original-context-unavailable");
 // Use the original saved leaf supplied by this exact callback, not the ELF
 // address now patched by the executor. No Context copy survives the callback.
 const auto leaf=context.original_leaf;
 if(!method_identity::Calibrate(context,leaf,methods.data(),methods.size(),&owner.methods_,request.error))return false;
 void*klass=nullptr;
 if(!runnable_executor::DecodeStrongGlobal(context,inputs.Anchor(),&klass))
  return Fail(request.error,"metadata-calibration-strong-Class-root-unavailable");
 method_array::View table;
 if(!owned_method_table::Calibrate(context,leaf,owner.methods_,klass,inputs.MethodCounts(),methods.data(),methods.size(),&table,request.error))return false;
 // Table and decoded Class are consumed here only; no frame or metadata
 // address is retained in the persistent calibration object.
 owner.mirrors_=executable_mirror::Layout::Probe(context,leaf,runnable_executor::DecodeStrongGlobal,methods.data(),methods.size(),request.error);
 if(!owner.mirrors_)return false;
 size_t marker=SIZE_MAX;
 for(size_t n=0;n<inputs.MethodsSchema().methods.size();++n){const auto&member=inputs.MethodsSchema().methods[n];
  if(member.name!="bootstrapNativeMarker"||member.descriptor!="()V")continue;
  for(size_t i=0;i<methods.size();++i)if(methods[i].dex_method_index==member.dex_index){
   if(marker!=SIZE_MAX)return Fail(request.error,"metadata-calibration-native-marker-is-not-unique");marker=i;
  }
 }
 if(marker==SIZE_MAX)return Fail(request.error,"metadata-calibration-native-marker-oracle-missing");
 owner.binding_=modern_methods::Service::BindOwned(request.hooks,context,leaf,methods[marker],CalibrationInputs::NativeMarker(),
  owner.runtime_.receivers.class_linker,owner.methods_.model,request.error);
 if(!owner.binding_||!owner.fields_.Calibrate(context,inputs.StorageClass(),inputs.FieldsSchema().descriptor.c_str(),fields.data(),fields.size(),request.error))return false;
 return field_mirror::Calibrate(context,owner.fields_,request.field_leaf,inputs.StorageClass(),fields.data(),fields.size(),&owner.field_mirrors_,request.error);
}
}

#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/native_source_observation_owner.h"
#include "platform/common/native_source_calibration_bridge.h"
#include "platform/common/jvmti_extensions.h"
namespace artdcevm::production_bootstrap {
// Sole process-lifetime owner of metadata and original-source calibration
// services. Hooks retain these addresses even after failed preparation. No
// Engine, private submission callback or legacy JIT/AOT commit is supplied.
class SourceCalibration final {
 public:
 static SourceCalibration*Prepare(HookEngine&,JNIEnv*,jvmtiEnv*,jobject,std::string*);
 SourceCalibration(const SourceCalibration&)=delete;SourceCalibration&operator=(const SourceCalibration&)=delete;
 const MetadataCalibration&Metadata()const noexcept{return *metadata_;}
 native_production_schema_capabilities::Owner&Schema()noexcept{return schema_;}
 native_source_class_loading::Service&Loading()noexcept{return observers_.Loading();}
 const native_source_stop::Service&Stop()const noexcept{return stop_;}
 const thread_suspend_service::Service&ThreadState()const noexcept{return state_;}
 private:
 SourceCalibration(HookEngine&hooks,std::unique_ptr<MetadataCalibration>metadata):hooks_(hooks),metadata_(std::move(metadata)){}
 ~SourceCalibration()=default;
 bool Install(JNIEnv*,std::string*);
 bool Calibrate(JNIEnv*,jvmtiEnv*,std::string*);
 struct Round;
 static bool Observe(const runnable_jni::Context&,void*);
 static void Begin(void*,const runnable_jni::Context&,void*)noexcept;
 static void End(void*,bool,void*)noexcept;
 HookEngine&hooks_;std::unique_ptr<MetadataCalibration>metadata_;
 native_source_observation::Owner observers_;
 native_production_schema_capabilities::Owner schema_;
 thread_suspend_service::Service state_;
 native_source_stop::Service stop_;
 bool bound_=false,installed_=false,poisoned_=false;
 size_t completed_=0;
};
}

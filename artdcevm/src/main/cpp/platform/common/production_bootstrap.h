#pragma once
#include "api/runtime_backend.h"
#include <memory>
#include <jvmti.h>
namespace artdcevm::production_bootstrap {
class Engine {
 public:
  virtual ~Engine()=default;
  virtual bool Source13Candidate()const=0;
  virtual void RecordDefinitions(JNIEnv*,const std::vector<Definition>&){}
  virtual bool FieldDeletionReady()const{return false;}
  virtual bool AtomicBatchReady()const{return false;}
  virtual bool SourceStopCalibrated()const{return false;}
  virtual std::string SourceStopDiagnostic()const{return "ordinary-source-lifecycle-not-observed";}
  virtual bool NativeScopeCalibrated()const{return false;}
  virtual bool SourceCapabilitiesCalibrated()const{return false;}
  virtual bool FrameGeometryCalibrated()const{return false;}
  virtual bool CompiledFramesCalibrated()const{return false;}
  virtual std::string FrameCalibrationDiagnostic()const{return "compiled-frame-calibration-not-attempted";}
  virtual std::string SourceFactoryDiagnostic()const{return "production-schema-services-unprepared";}
  virtual bool ReceiverInvokeSourceCalibrated()const{return false;}
  virtual std::string ReceiverInvokeSourceDiagnostic()const{return "receiver-invoke-source-not-observed";}
  virtual bool Activate(JNIEnv*,std::string*)=0;
  virtual int Apply(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*)=0;
};
// Only original installed APK DEX and public JVMTI/reflection observations may
// construct this engine. A source-family label never authorizes any private ABI.
std::unique_ptr<Engine> Prepare(JNIEnv*,jvmtiEnv*,jobject,std::string*);
}

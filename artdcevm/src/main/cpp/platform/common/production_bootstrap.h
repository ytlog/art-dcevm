#pragma once
#include "api/runtime_backend.h"
#include <memory>
#include <jvmti.h>
namespace artdcevm::production_bootstrap {
class Engine {
 public:
  virtual ~Engine()=default;
  virtual bool FieldDeletionReady()const{return false;}
  virtual bool AtomicBatchReady()const{return false;}
  virtual bool SourceStopCalibrated()const{return false;}
  virtual std::string SourceStopDiagnostic()const{return "ordinary-source-lifecycle-not-observed";}
  virtual bool NativeScopeCalibrated()const{return false;}
  virtual bool SourceCapabilitiesCalibrated()const{return false;}
  virtual std::string SourceFactoryDiagnostic()const{return "production-schema-services-unprepared";}
  virtual bool InitializerEpisodeObserved()const{return false;}
  virtual std::string InitializerEpisodeDiagnostic()const{return "initializer-source-episode-not-observed";}
  virtual bool ReceiverInvokeSourceCalibrated()const{return false;}
  virtual std::string ReceiverInvokeSourceDiagnostic()const{return "receiver-invoke-source-not-observed";}
  virtual bool ReceiverEntrySourceCalibrated()const{return false;}
  virtual std::string ReceiverEntrySourceDiagnostic()const{return "receiver-entry-source-not-observed";}
  virtual bool ReceiverHandleSourceCalibrated()const{return false;}
  virtual std::string ReceiverHandleSourceDiagnostic()const{return "receiver-handle-source-not-observed";}
  virtual bool ReceiverOriginalEntriesObserved()const{return false;}
  virtual std::string ReceiverOriginalEntriesDiagnostic()const{return "receiver-original-entries-not-observed";}
  virtual bool RootProvenanceObserved()const{return false;}
  virtual std::string RootProvenanceDiagnostic()const{return "runtime-root-observation-not-attempted";}
  virtual std::string RootProvenanceReport()const{return "{}";}
  virtual bool TypedHeapCalibrated()const{return false;}
  virtual std::string TypedHeapDiagnostic()const{return "typed-heap-source-not-observed";}
  virtual bool Activate(JNIEnv*,std::string*)=0;
  virtual int Apply(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*)=0;
};
// Private construction remains unavailable until the replacement transaction
// callgraph/bootstrap proves execution safety. Preparation currently returns
// nullptr with a diagnostic before any JNI, hooks, calibration or mutation.
// Pure installed-DEX metadata helpers live in bootstrap_dex_oracle.h/.cc.
std::unique_ptr<Engine> Prepare(JNIEnv*,jvmtiEnv*,jobject,std::string*);
}

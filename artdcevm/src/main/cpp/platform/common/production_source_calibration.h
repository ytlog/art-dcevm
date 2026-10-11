#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/native_source_observation_owner.h"
#include "platform/common/native_source_calibration_bridge.h"
#include "platform/common/jvmti_extensions.h"
#include "platform/common/native_call_graph_commit.h"
#include <utility>
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
 const native_call_graph_commit::Binding&CommitBinding()const noexcept{return commit_;}
 const native_structural_reflection::Service&Reflection()const noexcept{return reflection_;}
 const native_typed_heap_walk::RootProvenance&RootProvenance()const noexcept{return roots_;}
 const native_typed_heap_walk::Service&Heap()const noexcept{return heap_;}
 // Prepare must be repeated from each transaction's actual ordinary Native
 // caller when OEM mutex arguments have a current-thread-specific oracle.
 native_cache::Service&Cache()noexcept{return cache_;}
 native_source_compilation::Service&Compilation()noexcept{return compilation_;}
 private:
 SourceCalibration(HookEngine&hooks,std::unique_ptr<MetadataCalibration>metadata):hooks_(hooks),metadata_(std::move(metadata)){}
 ~SourceCalibration()=default;
 bool Install(JNIEnv*,std::string*);
 bool Calibrate(JNIEnv*,jvmtiEnv*,std::string*);
 bool PrepareCommitReaders(JNIEnv*,std::string*);
 bool PrepareTypedHeap(JNIEnv*,std::string*);
 struct RootLocks;
 static bool ObserveRootLocks(const runnable_jni::Context&,void*);
 struct Round;
 static bool Observe(const runnable_jni::Context&,void*);
 static void Begin(void*,const runnable_jni::Context&,void*)noexcept;
 static void End(void*,bool,void*)noexcept;
 HookEngine&hooks_;std::unique_ptr<MetadataCalibration>metadata_;
 native_source_observation::Owner observers_;
 native_production_schema_capabilities::Owner schema_;
 thread_suspend_service::Service state_;
 native_source_stop::Service stop_;
 native_call_graph_commit::Binding commit_;
 native_structural_reflection::Service reflection_;
 native_typed_heap_walk::RootProvenance roots_;
 native_cache::Service cache_;
 native_source_compilation::Service compilation_;
 exclusive_owned_roots::Service exclusive_;
 native_typed_heap_walk::Service heap_;
 bool exclusive_bound_=false;
 bool commit_bound_=false,reflection_bound_=false,roots_bound_=false,cache_bound_=false,compilation_bound_=false;
 bool bound_=false,installed_=false,poisoned_=false;
 size_t completed_=0;
};
}

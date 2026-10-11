#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/native_source_observation_owner.h"
#include "platform/common/native_source_calibration_bridge.h"
#include "platform/common/jvmti_extensions.h"
#include "platform/common/native_call_graph_commit.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/native_reference_decoder.h"
#include "platform/common/stack_visitor_calibration.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_receiver_source_calibration.h"
#include "platform/common/native_receiver_listener.h"
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
 retirement::Owners Retired()noexcept{return {membership_,retired_fields_,retired_methods_};}
 native_interpretation_registry::Registry&Interpretation()noexcept{return interpretation_;}
 native_reference_decoder::Decoder&References()noexcept{return references_;}
 // Constructor storage only; actual receiver events and complete stopped
 // frame walks must independently prove their own consumers.
 const stack_visitor_service::Declaration*StackDeclaration()const noexcept{return stack_calibrated_?&stack_declaration_:nullptr;}
 // Read-only code/identity sample calibration, not actual invoke-event coverage.
 receiver_guard::SourceProducer*ReceiverProducer()noexcept{return receiver_code_calibrated_?&receiver_producer_:nullptr;}
 const receiver_guard::SourceCalibration*ReceiverInvocations()const noexcept{
  return receiver_calibration_&&receiver_calibration_->Proven()?receiver_calibration_.get():nullptr;
 }
 private:
 SourceCalibration(HookEngine&hooks,std::unique_ptr<MetadataCalibration>metadata):hooks_(hooks),metadata_(std::move(metadata)){}
 ~SourceCalibration()=default;
 bool Install(JNIEnv*,std::string*);
 bool Calibrate(JNIEnv*,jvmtiEnv*,std::string*);
 bool PrepareCommitReaders(JNIEnv*,std::string*);
 bool PrepareTypedHeap(JNIEnv*,std::string*);
 bool PrepareRetirement(JNIEnv*,jvmtiEnv*,std::string*);
 bool PrepareFieldEntries(JNIEnv*,std::string*);
 bool PrepareMethodEntries(JNIEnv*,std::string*);
 bool PrepareFieldVarHandles(JNIEnv*,std::string*);
 bool PrepareFieldMethodHandles(JNIEnv*,const std::array<jobject,2>&,std::string*);
 struct FieldHandles;
 static bool ObserveFieldHandles(const runnable_jni::Context&,void*);
 bool PrepareInterpretation(JNIEnv*,std::string*);
 bool PrepareReferences(JNIEnv*,std::string*);
 bool PrepareStack(JNIEnv*,std::string*);
 bool PrepareReceiverCode(JNIEnv*,std::string*);
 bool PrepareReceiverEvents(JNIEnv*,std::string*);
 struct ReceiverEvents;
 static bool InstallReceiverEvents(const runnable_jni::Context&,void*);
 struct ReceiverCode;
 static bool ObserveReceiverCode(const runnable_jni::Context&,void*);
 static void*ReferenceCurrent(void*);
 static bool ReferenceOracle(void*,const runnable_jni::Context&,jobject,void**);
 static bool ReferenceScope(void*,JNIEnv*,jobject,runnable_jni::Operation,void*);
 struct Interpretation;
 static bool ObserveInterpretation(const runnable_jni::Context&,void*);
 struct Allocator;
 static bool CollectAllocator(const runnable_jni::Context&,void*);
 static bool ProveAllocator(const exclusive_owned_roots::Context&,void*);
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
 retirement::Membership membership_;
 retirement::RetiredFieldRegistry retired_fields_;
 retirement::RetiredMethodRegistry retired_methods_;
 native_interpretation_registry::Registry interpretation_;
 native_reference_decoder::Decoder references_;
 abi::arm64::android11::CurrentThread reference_current_=nullptr;
 bool references_bound_=false;
 stack_visitor_service::Declaration stack_declaration_=stack_visitor_service::Declaration::kAndroid12;
 bool stack_calibrated_=false;
 receiver_guard::SourceProducer receiver_producer_;
 bool receiver_bound_=false,receiver_code_calibrated_=false;
 std::unique_ptr<receiver_guard::FrameReader>receiver_frames_;
 std::unique_ptr<receiver_guard::BytecodeSource>receiver_bytecode_;
 std::unique_ptr<receiver_guard::SourceCalibration>receiver_calibration_;
 std::unique_ptr<receiver_guard::ListenerOwner>receiver_listener_;
 native_pending_exception::Service pending_exception_;
 bool receiver_listener_bound_=false;
 bool interpretation_bound_=false,interpretation_attempted_=false,interpretation_ready_=false;
 bool exclusive_bound_=false;
 bool commit_bound_=false,reflection_bound_=false,roots_bound_=false,cache_bound_=false;
 bool bound_=false,installed_=false,poisoned_=false;
 size_t completed_=0;
};
}

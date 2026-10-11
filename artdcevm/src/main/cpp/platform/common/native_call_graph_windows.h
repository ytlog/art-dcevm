#pragma once
#include "platform/common/native_call_graph_payload.h"
#include "platform/common/readable_mapping_scope.h"
#include "platform/common/production_installed_dex.h"

namespace artdcevm::native_call_graph_windows {
struct Bindings {
 native_schema_guard_verifier::Verifier*schema=nullptr;
 native_schema_guard_verifier::Bindings verifier;
 native_hierarchy_graph::Bindings hierarchy;
 receiver_guard::SourceProducer*source=nullptr;
 const production_bootstrap::InstalledDex*installed=nullptr;
 const stack_visitor_service::Declaration*stack_declaration=nullptr;
 const native_call_graph_super::Resolver*super_resolver=nullptr;
 native_call_graph_payload::Bindings payload;
 final_write_guard::NativeAccess*final_writer=nullptr;
};
// Owns the actual Verified/Collected preparation callbacks. Construct after
// the Run owner and destroy after RunBatch/end_run. Final incoming inventories
// are captured and owned here. Stage the operation's verifier and hierarchy
// directly from final inputs; both dependency objects outlive this factory.
// No substitute Holder or caller-selected preparation callback is accepted.
class Factory final:private source_handle_roots::StoppedReader,
                    private native_schema_guard_verifier::AccessPermissions {
 public:
 Factory(native_call_graph_run::Owner&owner,const Bindings&bindings);
 ~Factory();Factory(const Factory&)=delete;Factory&operator=(const Factory&)=delete;
 // The single synchronous transaction entry. Acquire the actual Direct lease,
 // route final-input preparation and both native graph windows, then invoke
 // RunBatch once. Close native owners before clearing the operation's verifier
 // and final-input reservation on every return, including preparation failure.
 jvmtiError Apply(jvmtiEnv*,const std::vector<Definition>&,std::string*);
 private:
 bool Attach(native_owned_removal::Provider*,std::string*);
 bool Stage(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*);
 void Finish()noexcept;
 struct RootedInput {
  bounded_managed_roots::Slot original{};
  const native_call_graph_incoming::Inventory*incoming=nullptr;
  bool submitted=true;
 };
 bool Ready()const noexcept;
 bool BeforePrepared()const noexcept;
 bool StageFinalAliases(JNIEnv*,std::string*);
 bool CaptureInputs(JNIEnv*,const std::vector<Definition>&,std::string*);
 bool PrepareBefore(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*);
 bool Preflight(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*);
 bool MatchInputs(JNIEnv*,const std::vector<Definition>&,const std::vector<RootedInput>&,std::string*)const;
 bool Window(JNIEnv*,jvmtiEnv*,const std::vector<jclass>&,
             const native_owned_removal::RootedGraph&,native_owned_removal::GraphWindow,std::string*);
 bool Definitions(const native_owned_removal::RootedGraph&,
                  std::vector<native_call_graph_replacement::Definition>*,std::string*);
 bool CaptureMappings(std::string*);
 bool BindRootRegistry(std::string*);
 bool RetainArrayRoots(std::string*);
 static void*RetainArray(void*,void*);
 static bool ReadArray(void*,const void*,void**)noexcept;
 static void ReleaseArray(void*,void*)noexcept;
 static bool ArrayStaging(void*)noexcept;
 bool Verify(const source_commit::Plan&,std::string*)const override;
 void BeginCommit(const source_commit::Plan&)const override;
 bool FinalRootWindow()const noexcept override;
 bool ReadStoppedRoot(const void*,void**)const noexcept override;
 static bool InsertPreparedRoot(void*,void*,size_t*);
 static bool ReadPreparedRoot(void*,size_t,void**)noexcept;
 static void ClearPreparedRoots(void*)noexcept;
 static uint64_t RootInsertionEpoch(void*)noexcept;
 rooted_schema_plan::Reader Reader()noexcept;
 static bool Extent(void*,const void*,size_t*)noexcept;
 static bool Decode(void*,jobject,void**)noexcept;
 static bool Readable(void*,const void*,size_t)noexcept;
 static void*Reference(void*,void*,size_t)noexcept;
 static bool ReleasePrivateRoots(void*,const char**)noexcept;
 enum class Phase {Prepared,Verified,Collected,Refused};
 native_call_graph_run::Owner&owner_;
 const native_call_graph_run::Bindings&run_;
 Bindings bindings_;
 bounded_managed_roots::Store*roots_=nullptr;
 std::vector<RootedInput>inputs_;
 std::vector<std::unique_ptr<native_call_graph_incoming::Inventory>>incoming_;
 std::vector<std::unique_ptr<native_call_graph_incoming::Inventory>>submitted_incoming_;
 std::unique_ptr<production_bootstrap::Provenance::Reservation>record_;
 uint64_t generation_=0;const void*holder_=nullptr;size_t holder_count_=0;
 std::unique_ptr<method_identity::ReadableMappingSnapshot>mappings_;
 modern_field_metadata::ReadContext read_{this,Extent};
 native_call_graph_payload::Payload payload_;
 // Outlives schema publication; explicitly closed before callback scope ends.
 std::unique_ptr<final_write_guard::AliasBatch>final_fields_;
 Phase phase_=Phase::Prepared;bool attached_=false,native_read_=false;
 bool roots_released_=false;
 jvmtiEnv*input_jvmti_=nullptr;
 bool preflight_attempted_=false,preflight_complete_=false;
 const std::vector<Definition>*preflight_inputs_=nullptr;
 bool inputs_staged_=false;
 bool recorded_=false;
 bool apply_attempted_=false;
 // Stable opaque slots in the one operation's GC-updated native pool. They
 // are not managed pointers and cannot grant read permission outside a stop.
 std::vector<void*>prepared_root_slots_;
 prepared_roots::Registry registry_{prepared_roots::Registry::Access{}};
};
}

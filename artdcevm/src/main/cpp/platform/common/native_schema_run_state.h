#pragma once
#include "platform/common/native_hierarchy_session.h"
#include "platform/common/native_hierarchy_receiver.h"
#include "platform/common/native_class_lookup.h"
#include "platform/common/native_required_cleanup.h"
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/native_owned_removal.h"
namespace artdcevm::native_schema_run_state {
// Public independently warmed resolved entries. Indices come from the installed
// DEX/public member oracle, never the candidate DexCache representation.
struct CacheCalibration {
 jobject strong_cache=nullptr,strong_expected_class=nullptr;
 size_t extent=0;
 native_hierarchy_cache::Counts counts{};
 uint32_t type_index=0,field_index=0,method_index=0;
 void* native_field=nullptr;void* native_method=nullptr;
};
struct Dependencies {
 HookEngine* hooks=nullptr;
 native_source_class_loading::Service* loading=nullptr;
 const native_source_stop::Service* stop=nullptr;
 source_handle_roots::Service* roots=nullptr;
 native_hierarchy_allocator::Service* allocator=nullptr;
 const native_source_dex_registry::Service* dex_registry=nullptr;
 const native_class_lookup::Service* lookup=nullptr;
 const whole_process_frames::Service* frames=nullptr;
 const native_required_cleanup::Binding* cleanup=nullptr;
 native_member_provider::Service* provider=nullptr;
 native_member_provider::Dependencies members{};
 prepared_roots::Registry* registry=nullptr;
 native_schema_guard_verifier::Verifier* guards=nullptr;
};
// Unique original Run aggregate. Stage, JNI roots and scanner storage precede
// reservation. It never uses the local Native JNI executor across a return.
// Continuous source-stop calibration is mandatory; original-final-only windows
// cannot make this transaction safely refundable and are not accepted here.
class RunState final {
 public:
 static std::unique_ptr<RunState> Begin(const Dependencies&,void* driver,
    const runnable_jni::Context&,jvmtiEnv*,const std::vector<native_hierarchy_graph::Input>&,
    const CacheCalibration&,std::string*);
 ~RunState();RunState(const RunState&)=delete;RunState&operator=(const RunState&)=delete;
 static RunState* Current()noexcept;
 // Typed allocator bridge for Functions::find. Actual observed linker/self and
 // source-owned loader Handle are mandatory; no SDK-selected declaration.
 static abi::arm64::android11::ReleaseObjectPointer FindOriginal(void*,void*,const char*,void*);
 bool BeforeVerification(void*,const transaction_root::Iterator*,std::string*);
 void AfterVerification(void*,const transaction_root::Iterator*,bool)noexcept;
 // One original successful Collected callback. Registers everything ordinary,
 // then enters calibrated actual continuous stop, resolves Existing slots,
 // scans every thread, seals hierarchy and prepares the concrete native facade.
 bool Collected(const native_owned_removal::RootedGraph&,source_commit::Plan&,std::string*);
 // Sole original Update boundary; refreshes only allocated pointer slots.
 std::unique_ptr<source_commit::Services> Finalize(const void* holder,const source_commit::Plan**)noexcept;
 void Commit(const source_commit::Plan&)noexcept;
 // Must occur at original fallible source boundary while Holder/DEX still live.
 void Refund()noexcept;
 // Called after original Run; releases only, never performs a late retirement.
 bool End(void* exact_driver,bool original_success)noexcept;
 // Read-only actual continuous owner proof for mapping/guard consumers.
 bool StoppedWindow()const noexcept;
 const whole_process_frames::Report& Frames()const noexcept;
 private:
 struct State;
 RunState(const Dependencies&,void*,const runnable_jni::Context&);
 bool Start(jvmtiEnv*,const std::vector<native_hierarchy_graph::Input>&,const CacheCalibration&,std::string*);
 std::unique_ptr<State> state_;
};
}

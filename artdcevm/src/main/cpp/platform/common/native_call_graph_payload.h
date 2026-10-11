#pragma once
#include "platform/common/native_call_graph_run.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/native_final_write_alias_batch.h"

namespace artdcevm::native_call_graph_payload {
struct Bindings {
 native_call_graph_commit::Dependencies commit;
 HookEngine*hooks=nullptr;
 const method_identity::Calibration*method_model=nullptr;
 const modern_methods::Binding*method_binding=nullptr;
 modern_methods::Service::Retired method_retired=nullptr;
 native_structural_reflection::Retirement retirement{};
 native_structural_reflection::HandleGuards handles{};
 rooted_schema_plan::Reader reader{};
 native_hierarchy_graph::Authority*authority=nullptr;
 void*class_linker=nullptr;
 // Prepared in ordinary Native; retained until the original executor returns.
 final_write_guard::AliasBatch*final_fields=nullptr;
};
// Operation-owned storage: construct before preparation and retain through
// original Run return, including failed/partial registration and end_run.
// SourceOwner borrows the Plan, method services and reflection plan. They must
// never be relocated or destroyed while its staged/final adapter is alive.
class Payload final {
 public:
 Payload()=default;Payload(const Payload&)=delete;Payload&operator=(const Payload&)=delete;
 // Only after the real Collected public window closed and a fresh native
 // mapping reader was installed. No JNI, managed allocation or suspension is
 // performed here; all managed operands belong to the actual Holder cohort.
 // The Run owner alone enters the source stop after this method returns.
 bool Prepare(const runnable_jni::Context&,const native_owned_removal::RootedGraph&,
              native_call_graph_run::Owner&,const Bindings&,prepared_roots::Registry&,std::string*);
 bool Prepared()const noexcept{return prepared_;}
 private:
 friend class native_call_graph_windows::Factory;
 source_commit::Plan plan_;
 std::vector<std::unique_ptr<modern_methods::Service>>methods_;
 std::vector<const modern_methods::Service*>method_views_;
 std::vector<native_schema_policy::Plan>policies_;
 std::unique_ptr<native_structural_reflection::Plan>reflection_;
 native_initialized_transfer::Proof initialization_;
 bool attempted_=false,prepared_=false;
};
}

#pragma once
#include "platform/common/native_call_graph.h"
#include "platform/common/native_call_graph_code.h"
#include "platform/common/native_call_graph_resolution.h"
#include "platform/common/native_call_graph_super.h"
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_receiver_producer.h"
#include <unordered_map>
#include "platform/common/native_schema_guard_verifier.h"
namespace artdcevm::native_call_graph_inventory {
// One synchronous operation, borrowing the verifier's independently rooted
// complete public census. No raw Class/loader address is retained across GC.
class Inventory final {
 public:
 Inventory()=default;Inventory(const Inventory&)=delete;Inventory&operator=(const Inventory&)=delete;
 bool Prepare(const runnable_jni::Context&,const native_schema_guard_verifier::Verifier&,
   const receiver_guard::SourceProducer&,const std::vector<void*>&changed_methods,bool changed_fields,std::string*);
 bool Prepare(const runnable_jni::Context&,const native_schema_guard_verifier::Verifier&,
   const receiver_guard::SourceProducer&,std::string*);
 bool ValidateOrdinary(const modern_field_metadata::ReadContext&)const noexcept;
 bool ValidateOrdinary(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 bool ResolveCalls(const runnable_jni::Context&,receiver_guard::SourceProducer&,std::string*);
 bool ResolveCalls(const runnable_jni::Context&,receiver_guard::SourceProducer&,const native_call_graph_super::Resolver&,std::string*);
 bool SealLoading(const native_source_class_loading::Run&,const void*driver,const modern_field_metadata::ReadContext&)noexcept;
 bool SealLoading(const native_source_class_loading::Run&,const void*driver,const modern_field_metadata::ReadContext&,const native_source_stop::Session&)noexcept;
 bool Validate(const modern_field_metadata::ReadContext&)const noexcept;
 bool Validate(const modern_field_metadata::ReadContext&,const native_source_stop::Session&)const noexcept;
 const native_call_graph::Plan& GraphPlan()const noexcept{return plan_;}
 const std::vector<native_call_graph::Node>& Nodes()const noexcept{return nodes_;}
 const std::vector<native_call_graph::Edge>& Edges()const noexcept{return edges_;}
 const std::vector<size_t>& Seeds()const noexcept{return seeds_;}
 const std::vector<native_selective_interpretation::Method>& Selection()const noexcept{return selection_;}
 struct ResolutionInput {void*caller;const receiver_guard::CodeView&code;const std::vector<unsigned char>&public_bytes;const native_call_graph::CallSite&site;};
 using ResolutionVisitor=bool(*)(void*,const ResolutionInput&);
 // Borrowed immutable facts, no declaration/dispatch permission. A resolver
 // may suspend; its results require a separate fresh rooted census before use.
 bool VisitCalls(ResolutionVisitor,void*)const;
 const bounded_managed_roots::Store* RootStore()const noexcept{return roots_;}
 private:
 friend class native_schema_guard_verifier::Verifier;
 struct Method {
  native_selective_interpretation::Method native;
  size_t declaring=SIZE_MAX,containing=SIZE_MAX;
  std::string owner,name,signature;
  std::vector<unsigned char> bytes;
  receiver_guard::CodeView code{};
  std::vector<native_call_graph::CallSite> calls;
  std::vector<bool> related;
  bool fields=false;
 };
 const native_schema_guard_verifier::Verifier*verifier_=nullptr;
 const receiver_guard::SourceProducer*source_=nullptr;
 const native_source_class_loading::Run*loading_=nullptr;const void*driver_=nullptr;void*self_=nullptr;
 const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0,census_revision_=0;
 struct Dispatch {void*declaration=nullptr;native_call_graph::InvokeKind kind=native_call_graph::InvokeKind::Virtual;
  bounded_managed_roots::Slot receiver{},declaring{};void*target=nullptr;};
 struct Super {size_t caller=SIZE_MAX,site=SIZE_MAX;bounded_managed_roots::Slot referenced{},parent{};
  void*declaration=nullptr;void*target=nullptr;uint16_t type_index=0,slot=0;
  evolution::MappedDexMethodReference reference;};
 std::vector<Super>supers_;const native_call_graph_super::Resolver*super_resolver_=nullptr;
 bool SealLoadingImpl(const native_source_class_loading::Run&,const void*,const modern_field_metadata::ReadContext&,const native_source_stop::Session*)noexcept;
 bool ResolveCallsImpl(const runnable_jni::Context&,receiver_guard::SourceProducer&,const native_call_graph_super::Resolver*,std::string*);
 std::vector<Dispatch>dispatches_;
 std::unordered_map<void*,size_t>method_ordinals_;
 std::vector<Method> methods_;std::vector<native_call_graph::Node>nodes_;
 std::vector<native_call_graph::Edge>edges_;std::vector<size_t>seeds_,affected_seeds_;
 std::vector<native_selective_interpretation::Method>selection_;
 native_call_graph::Plan plan_{};bool prepared_=false,sealed_=false,resolved_=false;
};
}

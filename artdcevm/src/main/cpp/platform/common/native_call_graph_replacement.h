#pragma once
#include "platform/common/native_call_graph_incoming.h"
#include "platform/common/native_call_graph_resolution.h"
#include "platform/common/native_owned_removal.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/native_call_graph_inventory.h"

namespace artdcevm::native_call_graph_replacement {
// One replacement Class from the actual loading Run's original Holder arrays.
// Stage independently roots old/new arrays, old/new Class and public loader in
// the caller's operation Store. That Store may also own the BEFORE selection.
// No raw managed object address persists across JNI/GC. Captured native methods
// remain owned by the rooted replacement Class's actual array.
class Binding final {
 public:
 Binding()=default;Binding(const Binding&)=delete;Binding&operator=(const Binding&)=delete;
 bool Stage(HookEngine&,const runnable_jni::Context&,jvmtiEnv*,
            const native_source_class_loading::Run&,const void*driver,
            const native_owned_removal::RootedGraph&,size_t partition,size_t class_index,
            const native_call_graph_incoming::Inventory&,
            bounded_managed_roots::Store&,const modern_field_metadata::FieldMetadata&,
            const method_identity::Calibration&,const receiver_guard::SourceProducer&,std::string*);
 // Allocation/JNI/OS-discovery free. Each successful validation requires the
 // actual stop, same live loading Run/Holder, original rooted array slot,
 // unchanged Store generation and complete current incoming DEX/method array.
 // This is method/root identity only: call resolution, union closure, stack
 // drain, native boundaries, GC references and publication remain independent.
 bool Validate(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 bool ValidateRefund(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 const std::vector<native_selective_interpretation::Method>&Selection()const noexcept{return prepared_?selection_:empty_;}
 size_t DeclaredCount()const noexcept{return prepared_?declared_.size():0;}
 private:
 friend class Batch;
 struct Declared {void*method=nullptr;uint32_t index=UINT32_MAX,flags=0;bool body=false;};
 enum class Purpose {Forward,Refund};
 bool Check(const modern_field_metadata::ReadContext&,Purpose=Purpose::Forward,bool pending_copies=false)const noexcept;
 bool HolderArrays(const modern_field_metadata::ReadContext&,Purpose=Purpose::Forward)const noexcept;
 bool ValidateFor(const native_source_stop::Session&,const modern_field_metadata::ReadContext&,Purpose)const noexcept;
 const native_call_graph_incoming::Inventory*incoming_=nullptr;
 const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0;
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 const receiver_guard::SourceProducer*source_=nullptr;
 const native_source_class_loading::Run*loading_=nullptr;
 const void*driver_=nullptr;const void*holder_=nullptr;void*self_=nullptr;size_t owner_count_=0,holder_index_=0,class_index_=0,class_count_=0;
 bounded_managed_roots::Slot old_array_{},new_array_{},old_class_{},new_class_{},loader_{},parent_{};
 std::vector<bounded_managed_roots::Slot>interfaces_;
 using RootBarrier=void*(*)(void*);RootBarrier root_=nullptr;
 method_metadata::Layout layout_{};void*array_=nullptr;
 uint32_t method_count_=0;
 struct Copy {
  Declared native;
  native_call_graph_inventory::Inventory::DeclarationSource source;
  const Binding*incoming_owner=nullptr;
  size_t incoming_ordinal=SIZE_MAX;
  native_selective_interpretation::Method incoming_declaration;
  const native_selective_interpretation::Method*Declaration()const noexcept {
   return incoming_owner?&incoming_declaration:source.declaration;
  }
  bounded_managed_roots::Slot Loader()const noexcept {
   return incoming_owner?incoming_owner->loader_:source.loader;
  }
 };
 std::vector<Copy>copied_;
 const native_call_graph_inventory::Inventory*before_=nullptr;
 std::vector<Declared>declared_;std::vector<native_selective_interpretation::Method>selection_;const std::vector<native_selective_interpretation::Method>empty_{};
 bool attempted_=false,prepared_=false;
};
struct Definition {
 size_t partition=SIZE_MAX,class_index=SIZE_MAX;
 const native_call_graph_incoming::Inventory*incoming=nullptr;
};
// All Classes in the supplied structural Holder partitions, exactly once.
// This batch owns bindings, not the caller's roots or incoming byte inventories.
// Complete binding is deliberately distinct from a complete union call graph.
class Batch final {
 public:
 bool Stage(HookEngine&,const runnable_jni::Context&,jvmtiEnv*,
  const native_source_class_loading::Run&,const void*driver,
  const native_owned_removal::RootedGraph&,const std::vector<Definition>&,
  bounded_managed_roots::Store&,const modern_field_metadata::FieldMetadata&,
  const method_identity::Calibration&,const receiver_guard::SourceProducer&,std::string*,
  const native_call_graph_inventory::Inventory*before=nullptr);
 bool Validate(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 // Identical identity/body checks, with failed loading permitted only while
 // the complete original Holder and exact pauser are still alive for refund.
 bool ValidateRefund(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 const std::vector<native_selective_interpretation::Method>&Selection()const noexcept{return prepared_?selection_:empty_;}
 size_t ClassCount()const noexcept{return prepared_?bindings_.size():0;}
 // Source identity only; neither graph completeness nor stop permission.
 bool OwnsSource(const native_source_class_loading::Run&,const void*driver,
                 const void*holder)const noexcept;
 const bounded_managed_roots::Store*RootStore()const noexcept{return prepared_?roots_:nullptr;}
 // Rooted replacement declarations and every actual incoming invoke. Exact
 // edges are same-Class direct/static declarations only. Other calls remain
 // explicit gaps until original resolution/dispatch and BEFORE union joining.
 // Even complete OutgoingPlan() covers this batch only, never union closure.
 // Loader tokens are this batch's rooted equivalence classes; union joining
 // must independently rebase them against the BEFORE roots. These symbolic
 // replacement targets do not attest the unpublished DexCache resolver state.
 const std::vector<native_call_graph::Node>&Nodes()const noexcept{return prepared_?nodes_:empty_nodes_;}
 const std::vector<native_call_graph::Edge>&Edges()const noexcept{return prepared_?edges_:empty_edges_;}
 const native_call_graph::Plan&OutgoingPlan()const noexcept{return prepared_?plan_:empty_plan_;}
 // Observe actual original resolution in the same live loading Run. May
 // suspend and leave an original exception pending. Returned declarations are
 // opaque native identities until joined to an independently rooted inventory.
 // This receipt never turns symbolic incoming edges into complete coverage.
 bool ResolveCalls(const runnable_jni::Context&,receiver_guard::SourceProducer&,std::string*,
  const native_call_graph_inventory::Inventory*before=nullptr,const native_call_graph_super::Resolver*super=nullptr);
 void*SuperTarget(void*caller,uint32_t pc)const noexcept;
 bool ValidateSupers(const runnable_jni::Context&,const modern_field_metadata::ReadContext&)const noexcept;
 const std::vector<native_call_graph_resolution::Result>&Resolutions()const noexcept{return resolved_?resolutions_:empty_resolutions_;}
 const native_selective_interpretation::Method*MethodAt(size_t ordinal)const noexcept {
  return prepared_&&ordinal<graph_methods_.size()?&graph_methods_[ordinal]:nullptr;
 }
 bool CallsResolved()const noexcept{return resolved_;}
 struct Class {
  bounded_managed_roots::Slot root{},parent{};
  std::vector<bounded_managed_roots::Slot>interfaces;
  uint32_t access=0;
 };
 const std::vector<Class>&Classes()const noexcept{return classes_;}
 const std::vector<native_selective_interpretation::Method>&Declarations()const noexcept{return graph_methods_;}
 bool ValidateOrdinary(const modern_field_metadata::ReadContext&)const noexcept;
 private:
 bool BuildGraph(const modern_field_metadata::ReadContext&,std::string*);
 bool CheckGraph(const modern_field_metadata::ReadContext&)const noexcept;
 bool ValidateFor(const native_source_stop::Session&,const modern_field_metadata::ReadContext&,Binding::Purpose)const noexcept;
 struct Root {const bounded_managed_roots::Store*store=nullptr;uint64_t generation=0;bounded_managed_roots::Slot slot{};};
 struct Super {size_t caller=SIZE_MAX,site=SIZE_MAX;Root referenced{},target_declaring{},target_containing{};
  Root declaration_declaring{},declaration_containing{};
  void*declaration=nullptr,*target=nullptr;uint16_t type=0,slot=0;bool interface_super=false;
  evolution::MappedDexMethodReference reference;};
 bool CaptureSuper(const native_call_graph_inventory::Inventory&,size_t,size_t,
  const native_call_graph_super::Observation&,std::string*);
 bool CheckSupers(const runnable_jni::Context*,const native_source_stop::Session*,
  const modern_field_metadata::ReadContext&)const noexcept;
 std::vector<Super>supers_;const native_call_graph_super::Resolver*super_resolver_=nullptr;
 std::vector<std::unique_ptr<Binding>>bindings_;
 std::vector<native_selective_interpretation::Method>selection_,empty_;
 struct NodeBinding {size_t binding=SIZE_MAX,declared=SIZE_MAX;bool copied=false;};
 const Binding::Declared*Carrier(const NodeBinding&)const noexcept;
 std::vector<NodeBinding>node_bindings_;
 // The executable source belongs to the declaration namespace, independently
 // of the Class array containing its carrier. In particular a copied method
 // must never use the containing Class's incoming method ordinal as its body.
 struct Source {
  const native_call_graph_incoming::Inventory*incoming=nullptr;
  const native_call_graph_inventory::Inventory*before=nullptr;
  uint32_t index=UINT32_MAX;
  receiver_guard::CodeView code{};
  std::vector<native_call_graph_incoming::Call>calls;
  std::vector<unsigned char>bytes;
  bool Matches(receiver_guard::CodeView&)const noexcept;
 };
 std::vector<Source>sources_;
 std::vector<Class>classes_;
 std::vector<native_selective_interpretation::Method>graph_methods_;
 std::vector<native_call_graph_resolution::Result>resolutions_,empty_resolutions_;
 bool resolution_attempted_=false,resolved_=false;
 std::vector<bounded_managed_roots::Slot>loaders_;
 std::vector<native_call_graph::Node>nodes_,empty_nodes_;
 std::vector<native_call_graph::Edge>edges_,empty_edges_;
 native_call_graph::Plan plan_{},empty_plan_{};
 const bounded_managed_roots::Store*roots_=nullptr;
 uint64_t generation_=0;bool attempted_=false,prepared_=false;
};
}

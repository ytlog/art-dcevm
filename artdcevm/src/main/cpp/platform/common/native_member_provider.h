#pragma once
#include "platform/common/native_method_table.h"
#include "platform/common/native_structural_reflection.h"
#include "platform/common/modern_source_commit.h"
namespace artdcevm::native_member_provider {
enum class Removal { kAny, kMethodsOnly, kFieldsOnly, kSchemaEvolution };
// Immutable bootstrap capabilities are borrowed for the entire synchronous ART
// transaction. The owner supplies persistent guarded retirement registries.
struct Dependencies {
 HookEngine* hooks=nullptr;
 const modern_methods::Binding* methods=nullptr;
 const method_identity::Calibration* method_model=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 const modern_field_storage::Service* fields=nullptr;
 native_cache::Service* cache=nullptr;
 runtime_services::Snapshot runtime;
 native_structural_reflection::Service* reflection=nullptr;
 native_structural_reflection::Retirement retirement;
 modern_methods::Service::Retired is_retired=nullptr;
 bool allow_field_removal=false;
 Removal expected_removal=Removal::kAny;
 native_structural_reflection::HandleGuards handle_guards;
 native_source_commit::Diagnostics* diagnostics=nullptr;
 const std::vector<native_schema_policy::Plan>* schemas=nullptr;
 const native_schema_policy::GuardVerifier* schema_guards=nullptr;
 // Borrowed for the entire synchronous Run; owner refreshes its stopped-window view.
 const modern_field_metadata::ReadContext* read_context=nullptr;
};
// One common algorithm prepares complete Method11/12 tables, reflective maps
// and final native commit services. It is used by production and debug owners.
class Service {
 public:
  std::unique_ptr<source_commit::Services> Prepare(const source_commit::Plan&,Dependencies,std::string*);
  bool CollectManagedRoots(source_commit::Services&,const prepared_roots::Registrar&);
  // Identity of the actual prepared provider, not a capability/stop grant.
  bool OwnsPreparedSource(const source_commit::Plan&,const native_source_commit::Services&)const noexcept;
  void ReleasePreparedSource(const native_source_commit::Services&)noexcept;
  bool RefreshManagedRoots(const source_commit::Plan&,source_commit::Services&,const prepared_roots::Resolver&)noexcept;
 private:
  friend class native_required_cleanup::Binding;
  static bool ReflectReady(const source_commit::Plan&,void*);
  static void ReflectRetire(const source_commit::Plan&,void*);
  static void ReflectUpdate(const source_commit::Plan&,void*);
  struct MemberFact{void* member;void* declaring;uint32_t flags,index;bool method;};
  struct ClassFact{void* klass;void* parent;void* loader;uint32_t flags;size_t instance_fields,static_fields,methods;void* instance_array=nullptr;void* static_array=nullptr;};
  bool CaptureFacts(const source_commit::Plan&);
  bool MatchFacts(const source_commit::Plan&)const noexcept;
  prepared_roots::Bindings roots_;
  std::vector<MemberFact> member_facts_;
  std::vector<ClassFact> class_facts_;
  source_commit::Services* prepared_source_=nullptr;
  bool refreshed_=false;
  const source_commit::Plan* plan_=nullptr;
  native_structural_reflection::Service* reflection_=nullptr;
  std::unique_ptr<native_structural_reflection::Plan> mapping_;
  std::vector<std::unique_ptr<modern_methods::Service>> tables_;
  std::vector<const modern_methods::Service*> views_;
  std::vector<native_schema_policy::Plan> schemas_;
  std::vector<native_schema_policy::GuardRequest> guard_requests_;
  const native_schema_policy::GuardVerifier* schema_guards_=nullptr;
  const modern_field_metadata::FieldMetadata* schema_metadata_=nullptr;
  const method_identity::Calibration* schema_model_=nullptr;
};
}

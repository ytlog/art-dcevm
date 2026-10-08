#pragma once
#include "platform/common/native_method_table.h"
#include "platform/common/native_structural_reflection.h"
#include "platform/common/modern_source_commit.h"
namespace artdcevm::native_member_provider {
enum class Removal { kAny, kMethodsOnly, kFieldsOnly };
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
};
// One common algorithm prepares complete Method11/12 tables, reflective maps
// and final native commit services. It is used by production and debug owners.
class Service {
 public:
  std::unique_ptr<source_commit::Services> Prepare(const source_commit::Plan&,Dependencies,std::string*);
 private:
  static bool ReflectReady(const source_commit::Plan&,void*);
  static void ReflectRetire(const source_commit::Plan&,void*);
  static void ReflectUpdate(const source_commit::Plan&,void*);
  const source_commit::Plan* plan_=nullptr;
  native_structural_reflection::Service* reflection_=nullptr;
  std::unique_ptr<native_structural_reflection::Plan> mapping_;
  std::vector<std::unique_ptr<modern_methods::Service>> tables_;
};
}

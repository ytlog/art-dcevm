#pragma once
#include "api/hook_engine.h"
#include "platform/common/source_commit.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/modern_method_services.h"
#include <memory>
#include "platform/common/executable_mirror_calibration.h"
#include <unordered_map>
#include "platform/common/resolution_invalidation_plan.h"
#include "platform/common/modern_field_storage.h"
#include "platform/common/native_schema_policy.h"
namespace artdcevm::native_structural_reflection {
// Callbacks check a process-lifetime guard owner and independently established
// runtime proof. A missing callback preserves conservative handle rejection.
struct HandleGuards {
  void* owner=nullptr;
  bool (*method_handles)(void*)=nullptr;
  bool (*field_var_handles)(void*)=nullptr;
  bool (*field_method_handles)(void*)=nullptr;
};
struct Retirement {
  void* owner=nullptr;
  bool (*method_retired)(void*,void*)=nullptr;
  bool (*field_retired)(void*,void*)=nullptr;
  void (*retire_method)(void*,void*)=nullptr;
  void (*retire_field)(void*,void*)=nullptr;
};
// Native provenance is prepared under the original structural Holder scope.
// Default absence is accepted only for unchanged/removal-only declarations.
struct ResolutionBinding {
  HookEngine* hooks=nullptr;
  const method_identity::Calibration* method_model=nullptr;
  const modern_field_storage::Service* storage=nullptr;
};
// All native pointers remain rooted by ART's actual structural Holder. Missing
// field retirement callbacks reject field deletion before publishing a plan.
// The provider, not this adapter, owns installed entry guards and GC scope.
class Plan {
 public:
  static std::unique_ptr<Plan> Prepare(const source_commit::Plan&,
      const modern_field_metadata::FieldMetadata&,
      const std::vector<const modern_methods::Service*>&,Retirement,std::string*,HandleGuards guards={},ResolutionBinding resolution={},const std::vector<native_schema_policy::Plan>* schemas=nullptr,const native_schema_policy::GuardVerifier* schema_guards=nullptr);
  bool HasDeletedMethods()const;
  bool HasDeletedFields()const;
 private:
  friend class Service;
  std::unique_ptr<resolution_invalidation::ResolutionInvalidationPlan> resolution_;
  Retirement retirement_;
  HandleGuards handle_guards_;
  std::unordered_map<void*,void*> methods_,fields_;
};
class Service {
 public:
  bool Bind(HookEngine&,void* observed_runtime,const executable_mirror::Layout&,std::string*);
  // Before mutation, reject a deleted method referenced through an unguarded
  // MethodHandle; guard completeness is never inferred from source kind or SDK.
  bool Validate(const Plan&,std::string*)const;
  void Retire(const Plan&)const;
  void Update(const Plan&)const;
 private:
  void* runtime_=nullptr;
  void* visit_=nullptr;
};
}

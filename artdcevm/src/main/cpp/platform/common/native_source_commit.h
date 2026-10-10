#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/native_initialized_transfer.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/modern_field_storage.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/runtime_services.h"
#include "platform/common/structural_methods.h"
#include "platform/common/object_replacement_map.h"
#include <memory>
#include "platform/common/prepared_field_copy.h"
namespace artdcevm::native_required_cleanup {class Binding;}
namespace artdcevm::native_source_commit {
enum class NativeRootPolicy {kPreserve, kRestore};
struct MethodPair {
  void* old_class=nullptr;
  void* new_class=nullptr;
  const structural_methods::Services* service=nullptr;
  void (*restore_old_roots)(const structural_methods::Services*)=nullptr;
};
// Reflective mappings/retirement must be completely prepared before mutation.
// The callback updates actual ART reflective roots, including dex-cache targets;
// it is not permitted to obtain JNI access or suspend in the native commit.
struct Reflective {
  bool (*prepared)(const source_commit::Plan&,void*)=nullptr;
  void (*update)(const source_commit::Plan&,void*)=nullptr;
  void* state=nullptr;
  // Optional publication after final Validate, never during Prepare.
  void (*begin_commit)(const source_commit::Plan&,void*)=nullptr;
  void (*rebind_static_fields)(const source_commit::Plan&,void*)=nullptr;
};
struct Diagnostics {
  size_t cleared_threads=0;
  bool declaring_roots_verified=false;
};
struct Dependencies {
  runtime_services::Snapshot observed;
  const modern_field_metadata::FieldMetadata* metadata=nullptr;
  const modern_field_storage::Service* fields=nullptr;
  native_cache::Service* caches=nullptr;
  Reflective reflective;
  NativeRootPolicy native_roots=NativeRootPolicy::kPreserve;
  // Snapshot null means unobserved, not proof that Runtime has no JIT.
  bool jit_absence_proven=false;
  Diagnostics* diagnostics=nullptr;
  bool prepare_field_copy=false;
  bool allow_removed_ancestors=false;
  // Request the narrow completed-initialization proof; never initializer removal permission.
  bool require_initialized_transfer=false;
  // Borrowed for the entire synchronous Run; owner refreshes its stopped-window view.
  const modern_field_metadata::ReadContext* read_context=nullptr;
};
// Fixed source heap-replacement policy is supplied by the original transaction
// provider. The complete field oracle selects only the shared Class scalar
// prefix. No SDK/BuildID gate authorizes either mutation or private policy.
// Binding does not authorize mutation. The owner must enter the original ART
// final exclusive-mutator / disabled-GC UpdateClassStructurally callback with
// the real RootHolder alive. Never call this from an arbitrary JNI native stop.
class Services final : public source_commit::Services {
 public:
  static std::unique_ptr<Services> Prepare(HookEngine&,const Dependencies&,
      const source_commit::Plan&,std::vector<MethodPair>,std::string* error);
  bool Validate(const source_commit::Plan&)const override;
  bool RetainsSourceField(const source_commit::Plan&,bool is_static,const void* source_class,const void* field,bool* retained)const noexcept;
  bool CollectManagedRoots(const prepared_roots::Registrar&);
  bool CanRefreshManagedRoots(const prepared_roots::Resolver&)const noexcept;
  void RefreshManagedRoots(const prepared_roots::Resolver&)noexcept;
  void BeginCommit(const source_commit::Plan&)const override;
  uint32_t Status(void*)const override;
  void SetStatus(void*,uint32_t)const override;
  void TransferInitialization(const source_commit::Plan&,size_t)const override;
  void CopyFields(bool,void*,void*,void*,void*)const override;
  void* ObjectClass(void*)const override;
  void MarkClassAndMethodsObsolete(void*,void*)const override;
  void UpdateReflectiveReferences(const source_commit::Plan&)const override;
  void ForceDeoptimizeFrames()const override;
  void ReplaceObjectReferences()const override;
  void RestoreDeclaringClasses(const source_commit::Plan&)const override;
  void* NewClassAfterReplacement(void*,size_t)const override;
  void RetainOldClass(void*,void*)const override;
  void InvalidateCompiledCode()const override;
  void DeoptimizeEverythingForAbstractTransition()const override;
  void ClearInterpreterCaches()const override;
 private:
  friend class native_required_cleanup::Binding;
  Services()=default;
  prepared_roots::Bindings roots_;
  native_initialized_transfer::Proof initialized_;
  Dependencies dependencies_;
  const source_commit::Plan* plan_=nullptr;
  std::vector<MethodPair> methods_;
  struct CopyPair{void* old_class;void* new_class;field_copy::PreparedPlan statics,instances;};
  std::vector<CopyPair> copies_;
  std::unique_ptr<object_replacement::ObjectReplacementMap> replacements_;
  void* self_=nullptr;
  const class_metadata::Layout* layout_=nullptr;
  void (*set_status_)(void*,uint8_t)=nullptr;
  void (*replace_)(void*,const void*)=nullptr;
  void (*retain_)(void*,void*)=nullptr;
  void (*invalidate_)(void*)=nullptr;
  void (*deopt_frames_)(void*)=nullptr;
  void (*deopt_everything_)(void*,const char*)=nullptr;
};
}

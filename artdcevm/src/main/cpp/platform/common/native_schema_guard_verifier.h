#pragma once
#include "platform/common/native_schema_policy.h"
#include "platform/common/native_schema_binding.h"
#include "platform/common/native_abstract_instances.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/prepared_root_bindings.h"
#include "platform/common/bounded_managed_roots.h"
#include <jni.h>
#include <jvmti.h>
#include <memory>
namespace artdcevm::native_schema_guard_verifier {
// A synchronous typed authority, owned by the exact original schema Run.
// Binding this interface grants nothing; each hierarchy request must re-prove
// its live native window, exact Plan and rooted allocator/receiver/cache owners.
class HierarchyAuthority {
 public:
  virtual ~HierarchyAuthority()=default;
  virtual bool VerifyHierarchy(const source_commit::Plan&,size_t,void* loader)const noexcept=0;
};
// Final-field enforcement belongs to the single installed permission owner.
// The verifier cannot manufacture readiness from a callback or a schema bit.
class FinalFields {
 public:
  virtual ~FinalFields()=default;
  virtual bool Verify(const source_commit::Plan&,size_t,void* loader,
      const evolution::ClassSchema&,const evolution::ClassSchema&,
      const native_schema_policy::Requirement&,std::string*)const=0;
  // Refresh the same prepared permission plan only after the verifier has
  // independently refreshed and checked its full frozen managed roots.
  virtual bool RefreshPlan(const source_commit::Plan&,std::string*)const{return false;}
  virtual bool VerifyAliases(const source_commit::Plan&,std::string*)const=0;
  virtual void BeginCommit(const source_commit::Plan&)const=0;
};
class AccessPermissions {
 public:
  virtual ~AccessPermissions()=default;
  // Proven process interpreter/deopt owner must cover future compiled/AOT
  // dispatch as well as currently active frames; flags alone are insufficient.
  virtual bool Verify(const source_commit::Plan&,std::string*)const=0;
  virtual void BeginCommit(const source_commit::Plan&)const=0;
};
struct Bindings {
  JavaVM* vm=nullptr;
  bounded_managed_roots::Bindings roots{};
  void* (*decode_native_field)(void*,void* owner,jfieldID)=nullptr;
  const modern_field_metadata::FieldMetadata* metadata=nullptr;
  const method_identity::Calibration* methods=nullptr;
  const native_abstract_instances::Service* abstract_instances=nullptr;
  const FinalFields* final_fields=nullptr;
  const AccessPermissions* access_permissions=nullptr;
  void* owner=nullptr;
  // Existing independently calibrated process owner; only the original
  // Runnable/native-rooted scope may call these. No JNI or suspension inside.
  bool (*decode_global)(void*,jobject,void**)=nullptr;
  void* (*decode_method)(void*,jmethodID)=nullptr;
  // Contextual field-ID alias proof created before the source pause. Never
  // reinterpret jfieldID; callback performs native-only lookup under roots.
  void* (*decode_field)(void*,jclass,jfieldID)=nullptr;
  // Ordinary Native JNI only; pin and round-trip public Field mirrors before
  // any native scan. No Manager field decoder declaration is required.
  bool (*stage_field)(void*,JNIEnv*,jclass,jfieldID,bool,std::string*)=nullptr;
  void* (*method_class)(void*,void*)=nullptr;
  bool (*same_method)(void*,void*,void*)=nullptr;
  // One mapping snapshot per native scan, refreshed under the native owner.
  // Lifetime is proved by Class/member roots; maps only attest permissions.
  bool (*readable)(void*,const void*,size_t)=nullptr;
  bool (*writable)(void*,const void*,size_t)=nullptr;
  // Extent from that same current snapshot, valid only in the caller's actual
  // GC-stable native window. Supplied denial must never invoke OS fallback.
  bool (*extent)(void*,const void*,size_t*)=nullptr;
};
enum class Window {Verified,Collected};
class Verifier final:public native_schema_policy::GuardVerifier {
 public:
  Verifier();~Verifier();
  Verifier(const Verifier&)=delete;Verifier& operator=(const Verifier&)=delete;
  // Public JNI stage before ART pauses definitions. All loaded caller Classes,
  // methods and direct interface identities are independently observed. Input
  // roots name the complete affected graph; caller supplies its existing lease.
  bool Stage(JNIEnv*,jvmtiEnv*,const std::vector<jclass>& input_roots,
             const Bindings&,std::string*);
  // Closing public mirrors may load a descendant. The outer preflight graph
  // must contain every such affected identity before source Run starts.
  bool MatchesAffected(JNIEnv*,const std::vector<jclass>&,std::string*)const;
  // Called by the two actual source booleans, never GetImplementedInterfaces:
  // fixed ART's latter calls ResolveDirectInterface and may load a class.
  bool CheckWindow(JNIEnv*,jvmtiEnv*,const std::vector<jclass>& old_classes,
                   Window,std::string*);
  // Original rooted native-only window, after public checks and before the
  // source irreversible point. Plan includes real Holder old/new instances.
  // Final provider Verify rechecks all native data against this same proof.
  bool Seal(const source_commit::Plan&,std::string*);
  // Prepare independent loaded/final/access proofs before managed roots freeze.
  // Hierarchy requests are deferred to the actual sealed native authority.
  bool PrepareGuardProofs(const source_commit::Plan&,
      const std::vector<native_schema_policy::GuardRequest>&,std::string*)const;
  bool UseHierarchyAuthority(const HierarchyAuthority*)noexcept;
  bool Verify(const source_commit::Plan&,size_t,void*,
              const native_schema_policy::Requirement&,std::string*)const override;
  bool VerifyBatch(const source_commit::Plan&,const std::vector<native_schema_policy::GuardRequest>&,std::string*)const override;
  void BeginCommit(const source_commit::Plan&)const override;
  bool CollectManagedRoots(const prepared_roots::Registrar&,std::string*);
  // Borrowed for this synchronous Run only, after ordinary registration froze
  // native slots. Its extent callback must prove the actual current stop owner;
  // denial remains authoritative and never falls back to OS discovery.
  bool UseStoppedReadContext(const modern_field_metadata::ReadContext*) noexcept;
  bool CanRefreshManagedRoots(const prepared_roots::Resolver&)const noexcept;
  bool RefreshManagedRoots(const source_commit::Plan&,const prepared_roots::Resolver&) noexcept;
  // Ordinary JNI, after the transaction gate is released; no permanent Class
  // roots, public method-ID caches or loader retention survive an operation.
  void Clear(JNIEnv*);
 private:
  struct State;std::unique_ptr<State> state_;
};
}

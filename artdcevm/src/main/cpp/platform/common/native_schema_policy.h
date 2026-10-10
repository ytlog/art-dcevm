#pragma once
#include "evolution/migration_plan.h"
#include "platform/common/source_commit.h"
namespace artdcevm::native_schema_policy {
// Declaration-only field delta policy used by the structural validation hook.
// It grants no layout or runtime permission and preserves that hook's boundary.

struct FieldEvolutionDecision {
  bool accepted = true;
  bool structural = false;
  bool final_guard = false;
  std::string error;
};
FieldEvolutionDecision CheckFieldEvolution(const std::vector<evolution::Member>& old_fields,
                                         const std::vector<evolution::Member>& new_fields);

// Legacy source-hook policy intentionally accepts fewer flag combinations than
// Plan. Moving its implementation does not grant broader mutation capabilities.
// Plain integer ABI: source adapters can request a runtime guard without moving
// platform C++ objects or policy implementations across the library boundary.
enum class ModifierEvolutionAction : int {
  kReject = 0,
  kUnchanged = 1,
  kRecreate = 2,
  kCheckFinalOverride = 3,
  kCheckAbstractDependents = 4,
  kCheckLoadedSubclass = 5,
};
ModifierEvolutionAction CheckMethodModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                     bool structural, bool new_direct);
ModifierEvolutionAction CheckClassModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                    bool structural);

enum class GuardKind {LoadedSubclasses,FinalMethodOverrides,AbstractMethodDependents,FinalFields,ExistingInstances,AccessPermissions,InterfaceIdentities,HierarchyGraph};
struct Requirement {GuardKind kind;std::string member_name,descriptor;};
struct GuardRequest {size_t class_pair;void* loader;Requirement requirement;};
// No constructor or class initializer is rerun on migrated objects. Incoming
// slots come from ART class/object allocation; initialized classes are not reinitialized.
// Only exact name/type/storage identities retain an old value or dispatch ID.
class Plan {
 public:
  static bool Prepare(const evolution::ClassSchema&,const evolution::ClassSchema&,Plan*,std::string*);
  const evolution::MigrationPlan& Migration()const{return migration_;}
  const std::vector<Requirement>& Requirements()const{return requirements_;}
  bool Matches(const evolution::ClassSchema&,const evolution::ClassSchema&)const;
  bool Requires(GuardKind)const;
  bool MethodIdentity(const evolution::Member&,const evolution::Member&)const;
 private:
  evolution::MigrationPlan migration_;
  std::vector<Requirement> requirements_;
};
// Called only with exact native Classes/loader under the original rooted,
// disabled-GC preparation scope. Absent proof refuses any required loaded-class
// or final-field check; a pure schema plan alone never grants mutation.
class GuardVerifier {
 public:
  virtual ~GuardVerifier()=default;
  virtual bool Verify(const source_commit::Plan&,size_t class_pair,void* loader,const Requirement&,std::string*)const=0;
  // One synchronous native proof episode. No JNI, safepoint or borrowed
  // pointer escape is permitted within the batch. This does not establish its
  // caller's disabled-GC/source lifetime; the original owner still must do so.
  virtual bool VerifyBatch(const source_commit::Plan& plan,const std::vector<GuardRequest>& requests,std::string* error)const {
   for(const auto& request:requests)if(!Verify(plan,request.class_pair,request.loader,request.requirement,error))return false;
   return true;
  }
  // Called only after final readonly proof, under the original exclusive stop.
  // Clear proven caller access shortcuts without JNI, allocation or failure.
  virtual void BeginCommit(const source_commit::Plan&)const=0;
};
uint32_t JavaFlags(uint32_t);
}

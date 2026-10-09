#pragma once
#include "api/hook_engine.h"
#include "platform/common/field_mirror_calibration.h"
#include "platform/common/retired_field_registry.h"
#include <vector>
namespace artdcevm::retirement {
// Ordinary Native JNI installation. Owner, registry and anchor strong global
// stay alive for the process; mirror declaration needs the real complete oracle.
// Covers JNI scalar/static access and FastNative Field getters/setters. Source
// dispatch and VarHandle remain separate capabilities. The real ART VisitTarget
// hook preserves its native source object and marks retired mirror indices.
bool InstallFieldEntryGuards(HookEngine&,JNIEnv*,jobject anchor_strong_global,
    void* observed_jni_id_manager,const field_mirror::Calibration&,
    RetiredFieldRegistry&,std::string* error);
// Register with the sole exported MethodHandle outer owner. This does not
// publish readiness; actual independent field handles must be prepared and
// original invocations proven through PrepareMethodHandleFieldGuards.
bool InstallMethodHandleFieldGuards(HookEngine&,std::string* error);
bool FieldEntryGuardsOwn(const RetiredFieldRegistry&);
bool RejectRetiredField(void* field,void* self,void* result);
// Called only by the sole outer MethodHandle owner after its independent kind
// and target calibration. Native current flags decide cached setter permission.
bool RejectFinalFieldSetter(void* field,void* self,void* result);
struct FieldVarHandleOracle {
  jobject strong_global=nullptr;
  void* observed_field=nullptr;
  jobject reflected_field=nullptr; // Borrowed strong global, independent JNI oracle.
};
// Public coordinate types and independent reflected modifiers/declaring owner
// classify static/instance handles without requiring a private static subclass.
// Two distinct owned instance/static targets are checked readonly inside real
// JNI Runnable access. Input globals need only live through this call. Actual
// native Access calls must subsequently observe every target before Ready.
// Repeated preparations independently prove new roots and refresh the observation
// epoch using the same durable hook; failure preserves the previous proof.
bool InstallFieldVarHandleGuards(HookEngine&,JNIEnv*,
    const std::vector<FieldVarHandleOracle>&,RetiredFieldRegistry&,std::string*);
struct FieldVarHandleGuardDiagnostics {
  bool installed=false,owner_matches=false,reader_prepared=false,named_visitor=false;
  uint64_t expected_mask=0,observed_mask=0,oracle_generation=0;
  uint64_t entries=0,field_entries=0,other_entries=0,target_seen_mask=0,
      original_true=0,original_false=0,context_failures=0,class_failures=0;
};
FieldVarHandleGuardDiagnostics InspectFieldVarHandleGuards(const RetiredFieldRegistry&);
bool FieldVarHandleGuardsReady(const RetiredFieldRegistry&);
std::string DescribeFieldVarHandleGuards(const RetiredFieldRegistry&);

bool MethodHandleFieldGuardsInstalled(const RetiredFieldRegistry&);
size_t RejectedFieldEntries();
}  // namespace artdcevm::retirement

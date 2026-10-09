#pragma once
#include "api/hook_engine.h"
#include "platform/common/retired_method_registry.h"
#include "platform/common/retired_field_registry.h"
#include <jni.h>
#include "platform/common/contextual_field_id.h"
#include <vector>
namespace artdcevm::retirement {
struct MethodHandleOracle {jobject strong_global; jmethodID method;};
struct FieldMethodHandleOracle {jobject strong_global; jfieldID field;
 std::shared_ptr<contextual_field_id::Proof> identity;
 // Independent source/public construction oracle, not inferred from a private
 // target or SDK. Unknown preserves retirement support but proves no final
 // setter capability. Fixed ART Kind: instance get/put=8/9, static get/put=10/11.
 uint32_t expected_kind=UINT32_MAX;};
// Registration is not readiness; prepare with actual targets and exercise each
// original exported invoke before allowing cached field handles in a commit.
bool RegisterMethodHandleFieldOwner(RetiredFieldRegistry&,std::string*);
bool PrepareMethodHandleFieldGuards(HookEngine&,JNIEnv*,void* manager,
    RetiredFieldRegistry&,const std::vector<FieldMethodHandleOracle>&,std::string*);
bool BeginMethodHandleFieldTargetProof(JNIEnv*,jobject strong_global,jfieldID,std::string*);
bool EndMethodHandleFieldTargetProof(bool invocation_succeeded,std::string*);
bool MethodHandleFieldGuardsReady(const RetiredFieldRegistry&);
// Calibrate the native VisitTarget callback against three distinct independently
// decoded JNI method identities. Does not publish readiness until an actual
// invocation traverses the stable exported MethodHandleInvoke/InvokeExact callee.
bool PrepareMethodHandleGuards(HookEngine&,JNIEnv*,void* manager,
    RetiredMethodRegistry&,const std::vector<MethodHandleOracle>&,std::string*);
bool BeginMethodHandleTargetProof(JNIEnv*,jobject strong_global,jmethodID,std::string*);
bool EndMethodHandleTargetProof(bool invocation_succeeded,std::string*);
bool MethodHandleGuardsReady(const RetiredMethodRegistry&);
}

#pragma once
#include "api/hook_engine.h"
#include "platform/common/field_mirror_calibration.h"
#include "platform/common/retired_field_registry.h"
namespace artdcevm::retirement {
// Ordinary Native JNI installation. Owner, registry and anchor strong global
// stay alive for the process; mirror declaration needs the real complete oracle.
// Covers JNI scalar/static access and FastNative Field getters/setters. Source
// dispatch and VarHandle remain separate capabilities. The real ART VisitTarget
// hook preserves its native source object and marks retired mirror indices.
bool InstallFieldEntryGuards(HookEngine&,JNIEnv*,jobject anchor_strong_global,
    void* observed_jni_id_manager,const field_mirror::Calibration&,
    RetiredFieldRegistry&,std::string* error);
size_t RejectedFieldEntries();
}  // namespace artdcevm::retirement

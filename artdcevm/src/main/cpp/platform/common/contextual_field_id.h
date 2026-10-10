#pragma once
#include "api/hook_engine.h"
#include "platform/common/retired_field_registry.h"
#include <jni.h>
#include <memory>
#include <string>
namespace artdcevm::contextual_field_id {
// Stable public JNI boundary. Public methods run only in Native JNI; only
// named Field::GetArtField/root decoding runs in the owned Runnable callback.
struct Proof;
bool Initialize(HookEngine&,std::string* error);
// Verifies FromReflectedField, Get[Static]FieldID and ToReflectedField against
// the same public Field/Class/name/type/modifiers before retaining a weak owner.
std::shared_ptr<Proof> Prepare(JNIEnv*,jobject reflected,jfieldID,
    std::string* error);
void* NativeField(const std::shared_ptr<Proof>&);
jfieldID Id(const std::shared_ptr<Proof>&);
bool IsStatic(const std::shared_ptr<Proof>&);
// Mirror is borrowed only for this call; no permanent strong user-loader root.
void* DecodeLive(JNIEnv*,jobject receiver,bool is_static,jfieldID,
    std::string* error);
// Run AFTER the exact registry retirement token is assigned, BEFORE mutation.
// No JNI call or Java allocation is performed here. Publication consumes the
// prevalidated proof; this is not a generic pointer->ID reconstruction.
void PublishRetired(const std::shared_ptr<Proof>&,retirement::RetiredFieldRegistry&);
// Publish only already independently captured aliases of this exact native ID.
void PublishNativeRetired(void*,retirement::RetiredFieldRegistry&);
// Exact final History plan consumers; no JNI, allocation, ordinary-reader mutex.
bool HasNativeProof(void*)noexcept;
void PublishNativeSurviving(void*,void*,retirement::RetiredFieldRegistry&)noexcept;
void* NativeSurvivingTarget(void*)noexcept;
// True only when namespace, static mode, ID and the exact retirement generation
// agree. Invalid/cleared aliases are removed by Prune outside Runnable scopes.
bool Retired(JNIEnv*,jobject receiver,bool is_static,jfieldID,
    retirement::RetiredFieldRegistry&);
// Native GC notification only invalidates; it never invokes JNI or clears roots.
void NativeNotifyPrune(retirement::RetiredFieldRegistry&);
void Prune(JNIEnv*,retirement::RetiredFieldRegistry&);
void Release(JNIEnv*,std::shared_ptr<Proof>*);
}

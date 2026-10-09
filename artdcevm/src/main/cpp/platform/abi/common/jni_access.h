#pragma once
#include <jni.h>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::common {
// Fixed 11--17 scoped_thread_state_change.h ValueObject-derived scope. Only
// inspect ART's live borrowed argument; these fields do not construct a scope.
struct RunnableJniScope {void* self;JNIEnv* env;void* vm;};
struct JniEnvironmentPrefix {decltype(JNIEnv::functions) functions;void* self;void* vm;};
using FromReflectedMethod=void* (*)(const RunnableJniScope&,jobject);
inline constexpr const char* kFromReflectedMethod=
    "_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject";
static_assert(sizeof(RunnableJniScope)==3*sizeof(void*));
static_assert(offsetof(RunnableJniScope,env)==sizeof(void*));
static_assert(sizeof(JniEnvironmentPrefix)==3*sizeof(void*));
static_assert(offsetof(JniEnvironmentPrefix,self)==sizeof(void*));
static_assert(sizeof(void*)==8, "Only 64-bit JNI ABIs are supported");
}

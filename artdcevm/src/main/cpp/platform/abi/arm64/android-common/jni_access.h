#pragma once
#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace artdcevm::abi::arm64::android_common {
// Fixed 11--17 ScopedObjectAccessAlreadyRunnable (empty ValueObject base).
// This describes the live argument supplied by ART, not a constructed scope.
struct RunnableJniScope {void* self;JNIEnv* env;void* vm;};
// Fixed leading JNIEnvExt fields (the remainder changes and is not declared).
struct JniEnvironmentPrefix {decltype(JNIEnv::functions) functions;void* self;void* vm;};
using FromReflectedMethod=void* (*)(const RunnableJniScope&,jobject);
inline constexpr const char* kFromReflectedMethod="_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject";
static_assert(sizeof(RunnableJniScope)==24 && offsetof(RunnableJniScope,env)==8);
static_assert(sizeof(JniEnvironmentPrefix)==24 && offsetof(JniEnvironmentPrefix,self)==8);
}

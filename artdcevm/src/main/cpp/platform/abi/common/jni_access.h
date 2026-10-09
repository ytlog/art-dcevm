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
// Fixed-width evidence carriers permit cross-host fixtures without pretending
// that a 32-bit remote address is a dereferenceable host pointer.
struct RunnableJniScope32 {uint32_t self,env,vm;};
struct RunnableJniScope64 {uint64_t self,env,vm;};
static_assert(sizeof(RunnableJniScope32)==12 && offsetof(RunnableJniScope32,env)==4);
static_assert(sizeof(RunnableJniScope64)==24 && offsetof(RunnableJniScope64,env)==8);
}

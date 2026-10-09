#pragma once
#include <jni.h>
namespace artdcevm::abi::common {
// Fixed 11--17 jni_id_manager.h: original C2 initializes the sole manager_
// pointer and starts deferred allocation; original D2 balances it. This scope
// does not read/write Runtime offsets or change JNI ID representation.
struct JniIdQueryScope {void* manager=nullptr;};
static_assert(sizeof(JniIdQueryScope)==sizeof(void*));
using JniIdQueryConstruct=void (*)(JniIdQueryScope*);
using JniIdQueryDestroy=void (*)(JniIdQueryScope*);
using DecodeMethodId=void* (*)(void*,jmethodID);
using DecodeFieldId=void* (*)(void*,jfieldID);
inline constexpr const char* kJniIdQueryConstruct="_ZN3art3jni34ScopedEnableSuspendAllJniIdQueriesC2Ev";
inline constexpr const char* kJniIdQueryDestroy="_ZN3art3jni34ScopedEnableSuspendAllJniIdQueriesD2Ev";
inline constexpr const char* kDecodeMethodId="_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID";
inline constexpr const char* kDecodeFieldId="_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID";
}

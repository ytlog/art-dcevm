#pragma once
#include <cstddef>
namespace artdcevm::abi::arm64::android_common {
// Fixed runtime/jni/jni_id_manager.h, android-11.0.0_r1 through 17.0.0_r1:
// no base or virtual methods; the sole field is JniIdManager* manager_.
// Original C2 populates it and D2 balances StartDefer/EndDefer. This does not
// switch pointer/index JNI ID mode. Both require SHARED mutator ownership.
struct JniIdQueryScope {void* manager;};
static_assert(sizeof(JniIdQueryScope)==sizeof(void*) && offsetof(JniIdQueryScope,manager)==0);
using JniIdQueryConstruct=void (*)(JniIdQueryScope*);
using JniIdQueryDestroy=void (*)(JniIdQueryScope*);
inline constexpr char kJniIdQueryConstruct[]="_ZN3art3jni34ScopedEnableSuspendAllJniIdQueriesC2Ev";
inline constexpr char kJniIdQueryDestroy[]="_ZN3art3jni34ScopedEnableSuspendAllJniIdQueriesD2Ev";
}

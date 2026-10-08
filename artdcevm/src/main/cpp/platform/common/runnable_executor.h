#pragma once
#include "api/hook_engine.h"
#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::runnable_executor {
// One process-lifetime owner for the two named JNI scope bridges. Initialize
// from regular Native JNI; exact symbol candidates are verified with an owned
// Java Thread identity inside a real JNI Runnable callback before availability.
// No SDK, Build ID, Thread/Runtime offsets or production layout token is used.
bool Initialize(HookEngine& hooks,JNIEnv* env,std::string* error);
bool Available();
// Input stays rooted throughout operation. Context and borrowed mirror pointers
// are valid only inside operation; establish an ART/JNI root before a safepoint
// or escape. Each invocation independently verifies the current Thread identity.
runnable_jni::Result With(JNIEnv* env,jobject strong_global,
                          runnable_jni::Operation operation,void* data);
// Use only inside With's current callback. A strong reference is required.
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject strong_global,void** output);
const char* Route();
}

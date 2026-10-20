#pragma once
#include <cstdint>
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <vector>
#include "platform/common/art_hook.h"

namespace artdcevm::constructor_history {
// A private JVMTI tag namespace models JDK reflection roots without retaining
// Constructor objects or their declaring loaders. ART still owns all targets.
bool Prepare(JavaVM* vm, JNIEnv* jni, std::vector<Hook>* hooks, std::string* why);
bool Available();
uint32_t CacheProofs();
const char* CacheReason();
// Atomic-only invalidation at ART's suspended commit, before threads resume.
void Invalidate() noexcept;
using NewInstance = jobject (*)(JNIEnv*, jobject, jobjectArray);
jobject Invoke(JNIEnv* jni, jobject constructor, jobjectArray args, NewInstance original, NewInstance fallback);
// Successful standard transactions invalidate fresh lookup roots. Existing
// copies keep the accessor they acquired, including a cached allocation error.
void Committed(JNIEnv* jni, jint count, const jvmtiClassDefinition* definitions);
}  // namespace artdcevm::constructor_history

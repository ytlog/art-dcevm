#pragma once
#include "platform/abi/arm64/android13/runtime.h"
namespace artdcevm::android13::runtime_access_abi {
using PlatformContainer=abi::arm64::android13::PlatformContainer;
using RuntimePrefix=abi::arm64::android13::RuntimePrefix;
struct Services { void* class_linker; void* jni_id_manager; void* jit; void* jit_code_cache; };
inline constexpr const char* kRuntimeInstance = "_ZN3art7Runtime9instance_E";
inline constexpr const char* kDecodeMethodId = "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID";
inline constexpr const char* kDecodeFieldId = "_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID";
}

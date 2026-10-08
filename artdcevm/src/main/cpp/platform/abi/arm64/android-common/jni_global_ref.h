#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::abi::arm64::android_common {
using ReleaseObjectPointer=android11::ReleaseObjectPointer;
// Fixed 11–17 release ObjPtr is passed as one uintptr_t register, established
// with the literal source and ARM64 compiler proof, not just sizeof equality.
using AddGlobalRef=jobject (*)(void* vm,void* self,ReleaseObjectPointer object);
inline constexpr const char* kAddGlobalRef="_ZN3art9JavaVMExt12AddGlobalRefEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEE";
}

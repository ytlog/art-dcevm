#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>
#include "platform/abi/arm64/android13/class_redefinition.h"

namespace artdcevm::android13::redefinition_abi {
// Fixed release arm64 declarations from android-13.0.0_r1:
// openjdkjvmti/ti_redefine.h and libdexfile/dex/dex_file.h.
// Platform std::string and ArrayRef remain opaque; no NDK C++ object is used
// to access or destroy them. The transitional module checks every used offset.
using ClassRedefinition=abi::arm64::android13::ClassRedefinition;
struct DexFilePrefix {
  const void* vtable;
  const unsigned char* begin;
  size_t size;
};
static_assert(sizeof(ClassRedefinition) == 72);
static_assert(sizeof(DexFilePrefix) == 24);
}

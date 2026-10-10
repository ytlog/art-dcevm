#pragma once

#include <cstdint>

namespace artdcevm::android13::suspend_abi {

// android-13.0.0_r1 runtime/{jvalue.h,obj_ptr.h,thread.h} and
// entrypoints/quick/quick_deoptimization_entrypoints.cc; release arm64 profile.
// Only the null ObjPtr representation is constructed here. No Thread fields
// or frame offsets are accessed by the enhancement library.
struct JValue { int64_t bits; };
struct ThrowablePtr { uintptr_t reference; };
enum class MethodType { kKeepDexPc = 0, kDefault = 1 };
using PushContext = void (*)(void*, const JValue&, bool, ThrowablePtr, bool, MethodType);
using Deoptimize = void (*)(void*);

}  // namespace artdcevm::android13::suspend_abi

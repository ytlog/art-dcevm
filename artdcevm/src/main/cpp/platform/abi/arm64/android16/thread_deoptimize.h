#pragma once
#include "platform/abi/arm64/android16/deoptimize_if_needed.h"
namespace artdcevm::thread_deoptimize_abi {
// Fixed Android16/17 runtime/deoptimization_kind.h. FullFrame remains 8;
// Android17 adds MethodHandleTypeMismatch=9 after it. This bridge requests
// full structural frame deoptimization only, never a compiler bailout kind.
enum class Kind {kFullFrame=8};
// Fixed release ObjPtr has one uintptr_t reference_; its nullptr constructor
// stores0, and kObjPtrPoisoning=kIsDebugBuild is disabled for this declaration.
// Only null is constructed. Debug/profile mismatches cannot issue a token.
struct NullThrowablePtr {uintptr_t reference;};
static_assert(sizeof(NullThrowablePtr)==8);
using PushContext = void (*)(void*,const deoptimize_if_needed_abi::JValue&,bool,
                            NullThrowablePtr,bool,deoptimize_if_needed_abi::MethodType);
using Deoptimize = deoptimize_if_needed_abi::OwnedContext (*)(void*,Kind,bool,bool);
inline constexpr const char* kPushContext="_ZN3art6Thread25PushDeoptimizationContextERKNS_6JValueEbNS_6ObjPtrINS_6mirror9ThrowableEEEbNS_24DeoptimizationMethodTypeE";
inline constexpr const char* kDeoptimize="_ZN3art6Thread10DeoptimizeENS_18DeoptimizationKindEbb";
}  // namespace artdcevm::thread_deoptimize_abi

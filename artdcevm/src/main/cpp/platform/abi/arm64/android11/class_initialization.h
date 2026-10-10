#pragma once
#include <cstddef>
namespace artdcevm::abi::arm64::android11 {
// Fixed ClassLinker declarations: Handle<Class> is a pointer to a live source
// compressed root, never a jobject or the Class address. ObjectLock& is opaque.
struct ClassHandle {const void* slot;};
static_assert(sizeof(ClassHandle)==sizeof(void*)&&alignof(ClassHandle)==alignof(void*));
using InitializeClass=bool(*)(void*,void*,ClassHandle,bool,bool);
using WaitForInitializeClass=bool(*)(void*,ClassHandle,void*,void*);
inline constexpr char kInitializeClass[]="_ZN3art11ClassLinker15InitializeClassEPNS_6ThreadENS_6HandleINS_6mirror5ClassEEEbb";
inline constexpr char kWaitForInitializeClass[]="_ZN3art11ClassLinker22WaitForInitializeClassENS_6HandleINS_6mirror5ClassEEEPNS_6ThreadERNS_10ObjectLockIS3_EE";
}

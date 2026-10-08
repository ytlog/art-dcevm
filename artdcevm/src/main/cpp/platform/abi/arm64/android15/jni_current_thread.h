#pragma once
#include "platform/abi/arm64/android11/jni_current_thread.h"
namespace artdcevm::abi::arm64::android15 {
using android11::CurrentThread;
using android11::kCurrentThread;
using CurrentPeer=void* (*)(void*);
inline constexpr const char* kCurrentPeer="_ZN3art6Thread22GetPeerFromOtherThreadEv";
// Fixed 15--17 nonconst declaration. Some API34 Mainline builds also use
// this declaration; select by matched module/source, not SDK or name fallback.
}

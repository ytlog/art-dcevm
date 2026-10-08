#pragma once
namespace artdcevm::abi::arm64::android11 {
using CurrentThread=void* (*)();
using CurrentPeer=void* (*)(const void*);
inline constexpr const char* kCurrentThread="_ZN3art6Thread14CurrentFromGdbEv";
inline constexpr const char* kCurrentPeer="_ZNK3art6Thread22GetPeerFromOtherThreadEv";
// Fixed 11--14 const declaration. Both getters must refer to the calling
// thread inside ART's live Runnable scope; peer is a borrowed raw mirror.
}

#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstdint>
namespace artdcevm::abi::arm64::android_common {
// Fixed mirror/class.cc resolved leaves. No allocating ResolveDirectInterface
// or Class initialization/suspension fallback is used by a receiver check.
struct DirectTypeIndex {uint16_t index;};
using DirectInterfaceType=DirectTypeIndex(*)(void*,uint32_t);
using LookupDirectInterface=android11::ReleaseObjectPointer(*)(void*,DirectTypeIndex,android11::ReleaseObjectPointer);
inline constexpr char kDirectInterfaceType[]="_ZN3art6mirror5Class25GetDirectInterfaceTypeIdxEj";
inline constexpr char kLookupDirectInterface[]="_ZN3art11ClassLinker20DoLookupResolvedTypeENS_3dex9TypeIndexENS_6ObjPtrINS_6mirror5ClassEEE";
using ObsoleteClass=android11::ReleaseObjectPointer(*)(void*);
using DirectInterface=android11::ReleaseObjectPointer(*)(void*,uint32_t);
inline constexpr char kObsoleteClass[]="_ZN3art6mirror5Class16GetObsoleteClassEv";
inline constexpr char kDirectInterface[]="_ZN3art6mirror5Class18GetDirectInterfaceEj";
}

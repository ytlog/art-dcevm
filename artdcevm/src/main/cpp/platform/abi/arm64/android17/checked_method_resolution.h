#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstdint>
namespace artdcevm::abi::arm64::android17 {
// Fixed runtime/class_linker.h and interpreter/mterp/nterp.cc release ABI.
// NterpGetMethod returns a tagged dispatch carrier, never a generic ArtMethod*.
struct MethodTypeIndex {uint16_t index;};
using MethodObjectPointer=android11::ReleaseObjectPointer;
using ResolveMethodClass=MethodObjectPointer(*)(void*,MethodTypeIndex,void*);
using FindResolvedMethod=void*(*)(void*,MethodObjectPointer,MethodObjectPointer,MethodObjectPointer,uint32_t);
using CheckedNterpMethod=uintptr_t(*)(void*,void*,const uint16_t*,uint32_t*);
inline constexpr char kResolveMethodClass[]="_ZN3art11ClassLinker13DoResolveTypeIPNS_9ArtMethodEEENS_6ObjPtrINS_6mirror5ClassEEENS_3dex9TypeIndexET_";
inline constexpr char kFindResolvedMethod[]="_ZN3art11ClassLinker18FindResolvedMethodENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEj";
inline constexpr char kCheckedNterpMethod[]="NterpGetMethod";
static_assert(sizeof(MethodTypeIndex)==2&&sizeof(MethodObjectPointer)==8);
enum class MethodDispatch:uint8_t {Virtual,Super,Direct,Static,Interface};
struct ResolvedDispatch {uintptr_t method;uint16_t method_index;uint32_t flags;bool object_class=false,string_class=false;};
// This interprets only the original checked source's successful carrier.
// The caller separately verifies the actual exception state and fresh selected
// method identity. No carrier or declaration alone supplies a guard proof.
inline bool DecodeCheckedMethod(MethodDispatch kind,uintptr_t carrier,
                                const ResolvedDispatch&resolved,uintptr_t*selected)noexcept {
 if(!selected||!resolved.method||(resolved.method&3u))return false;
 uintptr_t result=resolved.method;
 switch(kind){
  case MethodDispatch::Virtual:if(carrier!=resolved.method_index)return false;break;
  case MethodDispatch::Super:if(!carrier||(carrier&3u))return false;result=carrier;break;
  case MethodDispatch::Interface:
   if(resolved.object_class){if(carrier!=(uintptr_t(resolved.method_index)<<16|1u))return false;}
   else if(carrier!=(resolved.method|((resolved.flags&0x400u)?0u:2u)))return false;
   break;
  case MethodDispatch::Direct:
   if(resolved.string_class&&(resolved.flags&0x10000u)&&!(resolved.flags&8u)){
    if(!(carrier&1u)||(carrier&2u)||(carrier&~uintptr_t{3})==0)return false;
   }else if(carrier!=resolved.method)return false;
   break;
  case MethodDispatch::Static:if(carrier!=resolved.method)return false;break;
  default:return false;
 }
 *selected=result;return true;
}

}

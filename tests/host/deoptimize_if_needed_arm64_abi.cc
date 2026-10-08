#include "platform/abi/arm64/android16/deoptimize_if_needed.h"
namespace abi=artdcevm::deoptimize_if_needed_abi;
// Compile only. The nontrivial libc++ return ABI must preserve the separate
// AAPCS64 x8 result address while arguments still occupy x0–x5.
__attribute__((noinline)) abi::OwnedContext ArtOwnedDeoptCall(
    abi::ReturnsContext fn,void* inst,void* thread,void** sp,
    abi::MethodType type,abi::JValue value,bool ref){
  return fn(inst,thread,sp,type,value,ref);
}
extern "C" __attribute__((noinline)) void ArtVoidDeoptCall(
    abi::ReturnsVoid fn,void* inst,void* thread,void** sp,
    abi::MethodType type,abi::JValue value,bool ref){
  fn(inst,thread,sp,type,value,ref);
}

extern "C" __attribute__((noinline)) void* ArtOwnedDeoptRelease(
    abi::ReturnsContext fn,void* inst,void* thread,void** sp,
    abi::MethodType type,abi::JValue value,bool ref){
  return fn(inst,thread,sp,type,value,ref).release();
}

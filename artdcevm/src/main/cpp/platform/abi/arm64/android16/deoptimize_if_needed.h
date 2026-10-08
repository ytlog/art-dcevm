#pragma once
#include "platform/abi/arm64/android14/deoptimize_if_needed.h"
#include <memory>
#include <cstdlib>
#include <type_traits>
namespace artdcevm::deoptimize_if_needed_abi {
// Full virtual declaration of Android 16/17 runtime/arch/context.h. No concrete
// Arm64Context fields or vtable offsets are inferred. Owned platform objects
// dispatch virtual destruction through their original native deleting dtor.
class QuickMethodFrameInfo;
class Context {
 public:
  virtual ~Context() = default;
  virtual void Reset() = 0;
  virtual void FillCalleeSaves(uint8_t*,const QuickMethodFrameInfo&) = 0;
  virtual void SetSP(uintptr_t) = 0;
  virtual void SetPC(uintptr_t) = 0;
  virtual void SetArg0(uintptr_t) = 0;
  virtual bool IsAccessibleGPR(uint32_t) = 0;
  virtual uintptr_t* GetGPRAddress(uint32_t) = 0;
  virtual uintptr_t GetGPR(uint32_t) = 0;
  virtual void SetGPR(uint32_t,uintptr_t) = 0;
  virtual bool IsAccessibleFPR(uint32_t) = 0;
  virtual uintptr_t GetFPR(uint32_t) = 0;
  virtual void SetFPR(uint32_t,uintptr_t) = 0;
  virtual void SmashCallerSaves() = 0;
  virtual void SetNterpDexPC(uintptr_t) {std::abort();}
  virtual void CopyContextTo(uintptr_t*,uintptr_t*) = 0;
};
using OwnedContext = std::unique_ptr<Context>;
static_assert(sizeof(OwnedContext)==sizeof(void*));
static_assert(!std::is_trivially_copyable_v<OwnedContext>);
// Return type is NOT present in the mangled method name. Only the calibrated
// source declaration can choose between the void and nontrivial return ABI.
using ReturnsContext = OwnedContext (*)(void*,void*,void**,MethodType,JValue,bool);
}  // namespace artdcevm::deoptimize_if_needed_abi

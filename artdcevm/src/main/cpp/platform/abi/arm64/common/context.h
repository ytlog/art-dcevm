#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::android_common {
class QuickMethodFrameInfo;
// Identical virtual prefix of fixed Android 11–17 Context. The unrepresented
// tail has DoLongJump/CopyContextTo differences and must never be called through
// this declaration. Native factory ownership uses the native virtual D0.
class ContextPrefix {
 public:
  virtual ~ContextPrefix()=default;
  virtual void Reset()=0;
  virtual void FillCalleeSaves(uint8_t*,const QuickMethodFrameInfo&)=0;
  virtual void SetSP(uintptr_t)=0;
  virtual void SetPC(uintptr_t)=0;
  virtual void SetArg0(uintptr_t)=0;
  virtual bool IsAccessibleGPR(uint32_t)=0;
  virtual uintptr_t* GetGPRAddress(uint32_t)=0;
  virtual uintptr_t GetGPR(uint32_t)=0;
};
using CreateContext=ContextPrefix* (*)();
inline constexpr const char* kCreateContext="_ZN3art7Context6CreateEv";
}

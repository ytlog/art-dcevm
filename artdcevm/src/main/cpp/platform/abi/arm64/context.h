#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::context {
// Fixed 11/12/13/14/16 tags and17 b753cf9 arch/context.h virtual prefix.
// Native Create owns concrete ARM64 storage. The declaration preserves every
// virtual slot through GetFPR; no architecture-private register-array offsets.
struct FrameInfo;
class Prefix {
 public:
  virtual ~Prefix()=default;
  virtual void Reset()=0;
  virtual void FillCalleeSaves(uint8_t*,const FrameInfo&)=0;
  virtual void SetSP(uintptr_t)=0;
  virtual void SetPC(uintptr_t)=0;
  virtual void SetArg0(uintptr_t)=0;
  virtual bool IsAccessibleGPR(uint32_t)=0;
  virtual uintptr_t* GetGPRAddress(uint32_t)=0;
  virtual uintptr_t GetGPR(uint32_t)=0;
  virtual void SetGPR(uint32_t,uintptr_t)=0;
  virtual bool IsAccessibleFPR(uint32_t)=0;
  virtual uintptr_t GetFPR(uint32_t)=0;
};
using Create=Prefix* (*)();
inline constexpr char kCreate[]="_ZN3art7Context6CreateEv";
}

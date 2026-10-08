#pragma once
#include "platform/abi/arm64/android16/deoptimize_if_needed.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::owned_context_consumer {
namespace abi=deoptimize_if_needed_abi;
inline constexpr size_t kGprSlots=34,kFprSlots=32;
// Supplied only by a provider after fixed complete Thread source declaration,
// offsetof checks and owned device calibration. These numeric fields do not
// establish ABI compatibility and must never be inferred from disassembly.
// The global modern Profile gates and OwnedBind prohibition remain unchanged.
enum class MarkingRegister {kNotReserved,kReserved};
struct ThreadRefreshDeclaration {
  uint16_t gc_marking_offset;
  uint16_t suspend_trigger_offset;
  MarkingRegister marking_register;
  bool Valid()const{
    return gc_marking_offset%4==0&&suspend_trigger_offset%8==0&&
           suspend_trigger_offset!=0&&
           (marking_register==MarkingRegister::kNotReserved||marking_register==MarkingRegister::kReserved);
  }
  uint32_t Payload()const{return uint32_t(gc_marking_offset)|(uint32_t(suspend_trigger_offset)<<16);}
};
// Native source virtual CopyContextTo preserves platform stack MTE untagging
// and HWASAN longjmp notification; its destructor dispatches platform D0.
// gprs[32] (unrestored XZR slot) carries OUR refresh protocol, not an ART value.
// Buffers require 34 and 32 uintptr_t slots respectively, aligned to 16 bytes.
void CopyAndDestroy(abi::OwnedContext context,uint32_t refresh_payload,
                    uintptr_t (&gprs)[kGprSlots],uintptr_t (&fprs)[kFprSlots]);
#if defined(__aarch64__)
[[noreturn]] void Jump(abi::OwnedContext context,const ThreadRefreshDeclaration& declaration);
#endif
}  // namespace artdcevm::owned_context_consumer
extern "C" void ArtDcevmCopyContextForLongJump(
    artdcevm::deoptimize_if_needed_abi::Context*,uintptr_t*,uintptr_t*,uint32_t);

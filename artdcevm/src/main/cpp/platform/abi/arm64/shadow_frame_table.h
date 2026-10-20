#pragma once
#include <type_traits>
#include <cstring>
#include "platform/abi/arm64/class_layout_table.h"
#include "platform/abi/arm64/android13/shadow_frame.h"
#include "platform/abi/arm64/android16/shadow_frame.h"
namespace artdcevm::abi::arm64 {
// libdexfile/dex/dex_instruction.h, identical Android 11--17. The existing
// virtual service selects the register for both range and varargs forms.
class InstructionOperands {
 public:
  explicit InstructionOperands(size_t count) : num_operands(count) {}
  virtual ~InstructionOperands() = default;
  virtual uint32_t GetOperand(size_t index) const = 0;
  const size_t num_operands;
};
using FramePc = uint32_t (*)(void*, bool);
template <typename Frame> uint32_t AccessFramePc(void* address, bool finish) {
  auto* frame = static_cast<Frame*>(address);
  uint32_t pc = frame->dex_pc;
  if constexpr (std::is_same_v<Frame, android13::ShadowFrame>) {
    if (frame->dex_pc_ptr) pc = static_cast<uint32_t>(frame->dex_pc_ptr - frame->dex_instructions);
    if (finish) frame->dex_pc_ptr = nullptr;
  }
  if (finish) frame->dex_pc = ~uint32_t{0};
  return pc;
}
template <typename Class> constexpr FramePc FramePcFor() {
  if constexpr (std::is_same_v<Class, android16::Class> || std::is_same_v<Class, android17::Class>)
    return AccessFramePc<android16::ShadowFrame>;
  return AccessFramePc<android13::ShadowFrame>;
}
using FrameReturn = bool (*)(void*, bool);
template <typename Frame> bool ReturnFrame(void* address, bool request) {
  // shadow_frame.h, fixed 11--17: kForcePopFrame = 1 << 1. Only the
  // independently proved, non-synchronized void BindTo entry uses this flag.
  auto* frame = static_cast<Frame*>(address);
  if (request) frame->frame_flags |= 2;
  return (frame->frame_flags & 2) != 0;
}
template <typename Class> constexpr FrameReturn FrameReturnFor() {
  if constexpr (std::is_same_v<Class, android16::Class> || std::is_same_v<Class, android17::Class>)
    return ReturnFrame<android16::ShadowFrame>;
  return ReturnFrame<android13::ShadowFrame>;
}
using FrameReference = uint32_t (*)(const void*, uint32_t);
template <typename Frame> uint32_t ReadFrameReference(const void* address, uint32_t index) {
  const auto* frame = static_cast<const Frame*>(address);
  if (index >= frame->number_of_vregs) return 0;
  uint32_t reference;
  std::memcpy(&reference, reinterpret_cast<const uint8_t*>(address) + offsetof(Frame, vregs) +
      (size_t{frame->number_of_vregs} + index) * sizeof(uint32_t), sizeof(reference));
  return reference;
}
template <typename Class> constexpr FrameReference FrameReferenceFor() {
  if constexpr (std::is_same_v<Class, android16::Class> || std::is_same_v<Class, android17::Class>)
    return ReadFrameReference<android16::ShadowFrame>;
  return ReadFrameReference<android13::ShadowFrame>;
}
}

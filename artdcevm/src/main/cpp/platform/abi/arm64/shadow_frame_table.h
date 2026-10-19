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

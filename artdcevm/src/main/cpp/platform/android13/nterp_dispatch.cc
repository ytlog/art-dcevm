#include "platform/android13/nterp_dispatch.h"

#include <atomic>

namespace artdcevm::android13 {
namespace {
std::atomic<bool> dispatch_checks{false};
void* class_linker = nullptr;
CheckedDispatchResolver checked_resolver = nullptr;
}  // namespace

bool DecodeNterpDispatch(const uint16_t* instruction, uint32_t* index,
                         DispatchInvokeType* type) {
  if (instruction == nullptr || index == nullptr || type == nullptr) return false;
  // Standard DEX invoke-virtual/super/direct/static/interface opcodes, in
  // both non-range and range forms. Invoke-polymorphic/custom use other ABIs.
  switch (instruction[0] & 0xffu) {
    case 0x6e: case 0x74: *type = DispatchInvokeType::kVirtual; break;
    case 0x6f: case 0x75: *type = DispatchInvokeType::kSuper; break;
    case 0x70: case 0x76: *type = DispatchInvokeType::kDirect; break;
    case 0x71: case 0x77: *type = DispatchInvokeType::kStatic; break;
    case 0x72: case 0x78: *type = DispatchInvokeType::kInterface; break;
    default: return false;
  }
  *index = instruction[1];
  return true;
}

bool ConfigureNterpDispatch(void* linker, CheckedDispatchResolver resolver) {
  if (linker == nullptr || resolver == nullptr) return false;
  class_linker = linker;
  checked_resolver = resolver;
  return true;
}
void RequireDispatchChecks() { dispatch_checks.store(true, std::memory_order_release); }
bool NeedsDispatchChecks() { return dispatch_checks.load(std::memory_order_acquire); }
bool GuardNterpInvoke(void* thread, void* caller, uint16_t* dex_pc) {
  if (!NeedsDispatchChecks()) return false;
  uint32_t index;
  DispatchInvokeType type;
  if (!DecodeNterpDispatch(dex_pc, &index, &type)) return false;
  // ART's named checked resolver checks cached and unresolved dispatch alike.
  // nullptr means ART installed the exception; the Nterp proxy returns before
  // executing its unchecked resolver path.
  return checked_resolver(class_linker, thread, index, caller, type) == nullptr;
}
}  // namespace artdcevm::android13

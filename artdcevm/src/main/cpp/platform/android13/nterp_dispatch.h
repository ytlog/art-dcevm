#pragma once

#include <cstdint>

namespace artdcevm::android13 {
// Fixed declaration from Android 13 libdexfile/dex/invoke_type.h.
enum class DispatchInvokeType : uint32_t {
  kStatic = 0, kDirect = 1, kVirtual = 2, kSuper = 3, kInterface = 4,
};
using CheckedDispatchResolver = void* (*)(void*, void*, uint32_t, void*, DispatchInvokeType);

// The caller supplies a valid ART DEX instruction. Both supported invoke
// formats (35c and 3rc) store method_idx in the second 16-bit code unit.
bool DecodeNterpDispatch(const uint16_t* instruction, uint32_t* index,
                         DispatchInvokeType* type);
bool ConfigureNterpDispatch(void* class_linker, CheckedDispatchResolver resolver);
void* DispatchClassLinker();
void RequireDispatchChecks();
bool NeedsDispatchChecks();
bool GuardNterpInvoke(void* thread, void* caller, uint16_t* dex_pc);
}  // namespace artdcevm::android13

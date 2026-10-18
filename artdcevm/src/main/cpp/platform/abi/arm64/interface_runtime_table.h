#pragma once
#include "platform/abi/arm64/android11/interface_runtime.h"
#include "platform/abi/arm64/android12/interface_runtime.h"
#include "platform/abi/arm64/android13/interface_runtime.h"
#include "platform/abi/arm64/android14/interface_runtime.h"
#include "platform/abi/arm64/android15/interface_runtime.h"
#include "platform/abi/arm64/android16/interface_runtime.h"
#include "platform/abi/arm64/android17/interface_runtime.h"

namespace artdcevm::abi::arm64 {
// kSourceAligned: fixed AOSP declarations, and the named services every stored
// device runtime of the release provides, but no device run of the interface
// suites. Admitted like a device-verified release; runtime probes still decide
// each transaction, and reports name the missing device validation.
enum class InterfaceRuntimeStatus { kPreparedOnly, kSourceAligned, kDeviceVerified };
struct InterfaceRuntimeEntry {
  int release, first_api, last_api;
  const InterfaceRuntime* runtime;
  InterfaceRuntimeStatus status;
};
// Source declarations are not device ABI proof. Prepared-only entries never
// participate in API admission or runtime candidate selection.
inline constexpr InterfaceRuntimeEntry kInterfaceRuntimes[] = {
    {11, 30, 30, &android11::kInterfaceRuntime, InterfaceRuntimeStatus::kSourceAligned},
    {12, 31, 32, &android12::kInterfaceRuntime, InterfaceRuntimeStatus::kSourceAligned},
    {13, 33, 33, &android13::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified},
    {14, 34, 34, &android14::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified},
    {15, 35, 35, &android15::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified},
    {16, 36, 36, &android16::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified},
    {17, 37, 37, &android17::kInterfaceRuntime, InterfaceRuntimeStatus::kSourceAligned},
};
constexpr bool IsAdmitted(InterfaceRuntimeStatus status) {
  return status != InterfaceRuntimeStatus::kPreparedOnly;
}
constexpr const InterfaceRuntime* RuntimeFor(int release) {
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.release == release && IsAdmitted(entry.status)) return entry.runtime;
  return nullptr;
}
constexpr bool IsAdmittedInterfaceApi(int api) {
  for (const auto& entry : kInterfaceRuntimes)
    if (api >= entry.first_api && api <= entry.last_api && IsAdmitted(entry.status)) return true;
  return false;
}
constexpr bool IsDeviceVerifiedInterfaceRelease(int release) {
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.release == release) return entry.status == InterfaceRuntimeStatus::kDeviceVerified;
  return false;
}
inline constexpr size_t kMaxInterfaceScopeHeader = [] {
  size_t size = 0;
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.runtime->scope_header_size > size) size = entry.runtime->scope_header_size;
  return size;
}();
}  // namespace artdcevm::abi::arm64

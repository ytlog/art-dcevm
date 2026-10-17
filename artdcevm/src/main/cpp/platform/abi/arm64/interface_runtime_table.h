#pragma once
#include "platform/abi/arm64/android11/interface_runtime.h"
#include "platform/abi/arm64/android12/interface_runtime.h"
#include "platform/abi/arm64/android13/interface_runtime.h"
#include "platform/abi/arm64/android14/interface_runtime.h"
#include "platform/abi/arm64/android15/interface_runtime.h"
#include "platform/abi/arm64/android16/interface_runtime.h"
#include "platform/abi/arm64/android17/interface_runtime.h"

namespace artdcevm::abi::arm64 {
enum class InterfaceRuntimeStatus { kPreparedOnly, kDeviceVerified };
struct InterfaceRuntimeEntry {
  int release, first_api, last_api;
  const InterfaceRuntime* runtime;
  InterfaceRuntimeStatus status;
  bool linked_iftable_acceptance;  // No early link-error service required.
};
// Source declarations are not device ABI proof. Only device-verified entries
// participate in API admission and runtime candidate selection.
inline constexpr InterfaceRuntimeEntry kInterfaceRuntimes[] = {
    {11, 30, 30, &android11::kPreparedInterfaceRuntime, InterfaceRuntimeStatus::kPreparedOnly, false},
    {12, 31, 32, &android12::kPreparedInterfaceRuntime, InterfaceRuntimeStatus::kPreparedOnly, false},
    {13, 33, 33, &android13::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified, false},
    {14, 34, 34, &android14::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified, false},
    {15, 35, 35, &android15::kPreparedInterfaceRuntime, InterfaceRuntimeStatus::kPreparedOnly, false},
    {16, 36, 36, &android16::kInterfaceRuntime, InterfaceRuntimeStatus::kDeviceVerified, true},
    {17, 37, 37, &android17::kPreparedInterfaceRuntime, InterfaceRuntimeStatus::kPreparedOnly, false},
};
constexpr const InterfaceRuntime* RuntimeFor(int release) {
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.release == release && entry.status == InterfaceRuntimeStatus::kDeviceVerified)
      return entry.runtime;
  return nullptr;
}
constexpr bool UsesLinkedIfTable(int release) {
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.release == release) return entry.linked_iftable_acceptance;
  return false;
}
constexpr bool IsDeviceVerifiedInterfaceApi(int api) {
  for (const auto& entry : kInterfaceRuntimes)
    if (api >= entry.first_api && api <= entry.last_api &&
        entry.status == InterfaceRuntimeStatus::kDeviceVerified) return true;
  return false;
}
inline constexpr size_t kMaxInterfaceScopeHeader = [] {
  size_t size = 0;
  for (const auto& entry : kInterfaceRuntimes)
    if (entry.runtime->scope_header_size > size) size = entry.runtime->scope_header_size;
  return size;
}();
}  // namespace artdcevm::abi::arm64

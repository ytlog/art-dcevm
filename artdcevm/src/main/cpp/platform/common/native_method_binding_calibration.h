#pragma once
#include "platform/common/method_identity_calibration.h"
namespace artdcevm::native_method_binding {
// Fixed Method12 arm64 declaration (Android 12 through fixed Android 17 source).
// RegisterNatives must have installed expected_binding in this owned native
// fixture before entering Runnable; no concurrent registrations are permitted.
// This validates the mutable JNI data slot without guessing an entry point.
inline bool Calibrate(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf,
    const method_identity::Oracle& oracle, const void* expected_binding,
    std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!context.self || !context.env || !context.vm || !leaf || !expected_binding ||
      !oracle.strong_global_method || (oracle.java_modifiers & 0x100u) == 0 ||
      context.env->ExceptionCheck() ||
      context.env->GetObjectRefType(oracle.strong_global_method) != JNIGlobalRefType)
    return fail("Invalid owned registered-native binding oracle");
  const abi::arm64::android_common::RunnableJniScope scope{
      context.self, context.env, context.vm};
  void* method = leaf(scope, oracle.strong_global_method);
  size_t extent = 0;
  method_metadata::Identity identity;
  if (!method || context.env->ExceptionCheck() ||
      !method_identity::ReadableMappingExtent(method, &extent) ||
      extent < sizeof(method_metadata::Method12) || uintptr_t(method) % 4)
    return fail("Registered native method has no readable source storage");
  uint32_t index;
  std::memcpy(&index, static_cast<const unsigned char*>(method) +
      offsetof(method_metadata::Method12, dex_method_index), sizeof(index));
  identity = {method_metadata::ReadAccessFlags(method), index};
  if (identity.dex_method_index != oracle.dex_method_index ||
      (identity.access_flags & 0x11fu) != (oracle.java_modifiers & 0x11fu))
    return fail("Registered native identity does not match independent oracles");
  uint64_t binding;
  std::memcpy(&binding, static_cast<const unsigned char*>(method) +
      offsetof(method_metadata::Method12, data), sizeof(binding));
  if (binding != uintptr_t(expected_binding))
    return fail("Native JNI data slot does not match public RegisterNatives");
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::native_method_binding

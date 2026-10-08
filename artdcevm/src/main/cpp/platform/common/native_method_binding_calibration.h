#pragma once
#include "platform/common/method_identity_calibration.h"
namespace artdcevm::native_method_binding {
// Fixed Method11/Method12 arm64 declarations, selected by the complete
// independent native/DEX/reflection identity calibration.
// RegisterNatives must have installed expected_binding in this owned native
// fixture before entering Runnable; no concurrent registrations are permitted.
// This validates the mutable JNI data slot without guessing an entry point.
inline bool Calibrate(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf,
    const method_identity::Oracle& oracle, const void* expected_binding,
    method_identity::Model model, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  const method_metadata::Layout* declaration=nullptr;
  switch(model){
    case method_identity::Model::kMethod11:declaration=&method_metadata::kAndroid11;break;
    case method_identity::Model::kMethod12Onward:declaration=&method_metadata::kAndroid12Onward;break;
    default:return fail("No independently calibrated native method model");
  }
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
      extent < declaration->size || uintptr_t(method) % 4)
    return fail("Registered native method has no readable source storage");
  if(!method_metadata::ReadIdentity(*declaration,method,extent,&identity))
    return fail("Registered native source identity cannot be read");
  if (identity.dex_method_index != oracle.dex_method_index ||
      (identity.access_flags & 0x11fu) != (oracle.java_modifiers & 0x11fu))
    return fail("Registered native identity does not match independent oracles");
  uint64_t binding;
  std::memcpy(&binding, static_cast<const unsigned char*>(method) +
      declaration->data, sizeof(binding));
  if (binding != uintptr_t(expected_binding))
    return fail("Native JNI data slot does not match public RegisterNatives");
  if (error) error->clear();
  return true;
}
// Existing modern callers retain their fixed declaration until their binding
// service accepts the independently calibrated identity model explicitly.
inline bool Calibrate(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf,
    const method_identity::Oracle& oracle,const void* binding,std::string* error){
  return Calibrate(context,leaf,oracle,binding,method_identity::Model::kMethod12Onward,error);
}
}  // namespace artdcevm::native_method_binding

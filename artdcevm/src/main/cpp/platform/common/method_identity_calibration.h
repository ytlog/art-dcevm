#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/art_method_metadata.h"
#include <string>
#include <vector>

namespace artdcevm::method_identity {
enum class Model { kMethod11, kMethod12Onward };
struct Oracle {
  jobject strong_global_method;
  uint32_t dex_method_index;
  uint32_t java_modifiers;
};
struct Calibration {
  Model model;
  std::vector<method_metadata::Identity> identities;
  std::vector<size_t> readable_extents;
};
// Reads the current process mapping that contains address. Mapping readability
// alone does not prove object lifetime; owned reflected Method and declaring
// Class roots must remain live through calibration and every subsequent use.
bool ReadableMappingExtent(const void* address, size_t* extent);
// Current-thread diagnostic count only; no caching or safety authority.
uint64_t MappingOsQueries()noexcept;
// Only call from an already calibrated live Runnable JNI callback. The named
// leaf is resolved from the loaded libart; never reinterpret a jmethodID as ART.
// Oracles come independently from the APK's DEX method_ids/class_data and Java
// reflection, never from candidate ART fields. At least three distinct methods
// (native, nonnative instance, nonnative static) are required. No managed object
// is dereferenced, and only immutable index plus atomic access flags are read.
// This proves identity field placement, not all fields or private evolution.
// No SDK/Build ID dispatch. Failure preserves output and pending exceptions.
bool Calibrate(const runnable_jni::Context& context,
               abi::common::FromReflectedMethod named_leaf,
               const Oracle* oracles, size_t count, Calibration* output,
               std::string* error);
#ifdef ARTDCEVM_TESTING
// Completed independent debug oracle; contains scalar identities, no roots.
Calibration DebugCalibration();
#endif
}  // namespace artdcevm::method_identity

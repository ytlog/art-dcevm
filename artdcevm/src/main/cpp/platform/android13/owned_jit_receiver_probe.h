#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include "platform/common/jit_service.h"
namespace artdcevm::android13 {
struct OwnedJitReceivers {
  void* jit=nullptr;
  void* code_cache=nullptr;
  void* heap=nullptr;
};
// A regular Native JNI caller warms the rooted EvolutionCalibration.compute
// method. Its original named JIT compile call establishes the receiver even on
// a compiler worker. An independently observed JNI manager decodes the public
// method ID. Original suspension and public ForceGC establish heap/cache.
// No Runtime slots, Jit fields or Build IDs produce receiver observations.
// Missing JIT, collector/LTO bypass, conflicting receivers or cleanup failures
// refuse calibration and preserve output. Hooks remain passthrough for process
// lifetime. Native pointers are borrowed for the original Runtime lifetime.
bool CalibrateOwnedJitReceivers(HookEngine& hooks,JNIEnv* env,
                               jobject calibration,void* observed_jni_manager,
                               OwnedJitReceivers* output,std::string* error);
namespace owned_jit_probe_abi {
// Fixed runtime/jit/jit.h: ScopedJitSuspend has one bool was_on_ member.
struct SuspendJit {unsigned char was_on;};
static_assert(sizeof(SuspendJit)==1 && alignof(SuspendJit)==1);
enum class GcCause : int {};
using Compile=jit_service::Compile12;
using Collect=void (*)(void*,bool,GcCause);
using Sweep=void (*)(void*,void*);
inline constexpr const char* kCompile=jit_service::kCompile12;
inline constexpr char kCollect[]="_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE";
inline constexpr char kSweep[]="_ZN3art3jit12JitCodeCache15SweepRootTablesEPNS_15IsMarkedVisitorE";
inline constexpr char kSuspend[]="_ZN3art3jit16ScopedJitSuspendC2Ev";
inline constexpr char kResume[]="_ZN3art3jit16ScopedJitSuspendD2Ev";
}
}

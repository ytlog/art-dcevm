#include "platform/android13/jni_method_targets.h"
#include "platform/android13/jni_method_targets_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/symbols/android13.h"
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
// ART base/enums.h declares PointerSize with size_t as its underlying type.
using CanonicalMethod = void* (*)(void*, size_t);
CanonicalMethod canonical_method = nullptr;
void* Canonicalize(void* method) {
  const uint32_t flags=__atomic_load_n(reinterpret_cast<uint32_t*>(
      static_cast<unsigned char*>(method)+offsetof(art_method_abi::ArtMethod,access_flags)),__ATOMIC_RELAXED);
  const bool copied=(flags & (0x80000000u | 0x01000000u))==0x01000000u;
  return copied ? canonical_method(method, size_t{8}) : method;
}
}

void* CanonicalizeJniMethod(void* method) { return Canonicalize(method); }

bool ConfigureJniMethodTargets(HookEngine& hooks, void* implementation, std::string* error) {
  using Probe = bool (*)(JniMethodTarget);
  auto probe = reinterpret_cast<Probe>(
      dlsym(implementation, "ArtDcevmSourceProbeJniMethodTargets"));
  auto canonical = reinterpret_cast<CanonicalMethod>(
      hooks.FindSymbol("libart.so", cas::kGetCanonicalMethod));
  if (!probe || !canonical) {
    *error = "Missing JNI method-target calibration or named ART canonical-method function";
    return false;
  }
  canonical_method = canonical;
  if (!probe(Canonicalize)) {
    *error = "JNI method-target fixed declaration calibration failed";
    return false;
  }
  return true;
}
}

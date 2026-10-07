#include "platform/android13/jni_method_targets.h"
#include "platform/android13/jni_method_targets_abi.h"
#include "platform/symbols/android13.h"
#include <cstddef>
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
// ART base/enums.h declares PointerSize with size_t as its underlying type.
using CanonicalMethod = void* (*)(void*, size_t);
CanonicalMethod canonical_method = nullptr;
void* Canonicalize(void* method, bool copied) {
  return copied ? canonical_method(method, size_t{8}) : method;
}
}

bool ConfigureJniMethodTargets(HookEngine& hooks, void* implementation, std::string* error) {
  using Configure = void (*)(JniMethodTarget);
  auto configure = reinterpret_cast<Configure>(
      dlsym(implementation, "ArtDcevmSourceConfigureJniMethodTargets"));
  auto canonical = reinterpret_cast<CanonicalMethod>(
      hooks.FindSymbol("libart.so", cas::kGetCanonicalMethod));
  if (!configure || !canonical) {
    *error = "Missing JNI method-target bridge or named ART canonical-method function";
    return false;
  }
  canonical_method = canonical;
  configure(Canonicalize);
  return true;
}
}

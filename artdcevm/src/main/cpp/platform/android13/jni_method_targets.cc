#include "platform/android13/jni_method_targets.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/symbols/android13/source_symbols.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13 {
namespace {
// ART base/enums.h declares PointerSize with size_t as its underlying type.
using CanonicalMethod = void* (*)(void*, size_t);
CanonicalMethod canonical_method = nullptr;
void* Canonicalize(void* method) {
  const uint32_t flags=art_method_abi::ReadAccessFlags(method);
  const bool copied=(flags & (0x80000000u | 0x01000000u))==0x01000000u;
  return copied ? canonical_method(method, size_t{8}) : method;
}
}

void* CanonicalizeJniMethod(void* method) { return Canonicalize(method); }

bool ConfigureJniMethodTargets(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!profile.Matched()) { *error = "JNI method targets require a matched device profile"; return false; }
  auto canonical = reinterpret_cast<CanonicalMethod>(
      hooks.FindSymbol("libart.so", ::artdcevm::symbols::android13::kGetCanonicalMethod));
  if (!canonical) {
    *error = "Missing named ART canonical-method function";
    return false;
  }
  canonical_method = canonical;
  return true;
}
}

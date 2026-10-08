#pragma once
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android12/reflection.h"
#include "platform/abi/arm64/android11/reflection.h"
#include <set>
namespace artdcevm::executable_mirror {
// Fixed scalar identity fields shared by android-11 through android-17.0.0_r1
// b753cf97923c runtime/mirror/executable.h. A source candidate, not SDK dispatch.
using Declaration = abi::arm64::android12::Executable;
static_assert(offsetof(Declaration, art_method) == 24);
static_assert(offsetof(Declaration, access_flags) == 32);
static_assert(offsetof(Declaration, dex_method_index) == 36);
static_assert(offsetof(abi::arm64::android11::Executable,art_method)==offsetof(Declaration,art_method));
static_assert(offsetof(abi::arm64::android11::Executable,access_flags)==offsetof(Declaration,access_flags));
static_assert(offsetof(abi::arm64::android11::Executable,dex_method_index)==offsetof(Declaration,dex_method_index));
// AccessibleObject/parameter bytes differ: this capability permits only the
// named pointer/access flags/DEX-index fields below, never that earlier tail.
using Decode = bool (*)(const runnable_jni::Context&, jobject, void**);
// Checks every owned Constructor/Method against the named ART leaf and the
// independent APK DEX/Java reflection oracles. No JNI calls or safepoints occur
// between mirror decoding and scalar reads. The result permits this mirror
// declaration only; it does not prove reflected Field or native method entries.
inline bool Calibrate(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf, Decode decode,
    const method_identity::Oracle* oracles, size_t count, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!context.self || !context.env || !context.vm || !leaf || !decode ||
      !oracles || count < 3 || context.env->ExceptionCheck())
    return fail("Invalid owned Executable mirror calibration");
  const abi::arm64::android_common::RunnableJniScope scope{
      context.self, context.env, context.vm};
  std::set<void*> distinct;
  constexpr uint32_t kJavaModifiers = 0x11fu;
  for (size_t i = 0; i < count; ++i) {
    const auto& oracle = oracles[i];
    if (!oracle.strong_global_method || oracle.dex_method_index == UINT32_MAX ||
        context.env->GetObjectRefType(oracle.strong_global_method) != JNIGlobalRefType ||
        context.env->ExceptionCheck()) return fail("Executable oracle is not a strong global");
    void* native = leaf(scope, oracle.strong_global_method);
    if (!native || context.env->ExceptionCheck()) return fail("Named Executable leaf failed");
    void* mirror = nullptr;
    if (!decode(context, oracle.strong_global_method, &mirror) || !mirror ||
        !distinct.insert(mirror).second) return fail("Executable mirrors are not distinct owned objects");
    size_t extent = 0;
    if (uintptr_t(mirror) % 4 || !method_identity::ReadableMappingExtent(mirror, &extent) ||
        extent < sizeof(Declaration)) return fail("Executable source prefix is not readable");
    // Preserve native tagged pointer identity; only mapping lookup normalizes it.
    Declaration observed;
    std::memcpy(&observed, mirror, sizeof(observed));
    if (observed.art_method != uintptr_t(native) ||
        observed.dex_method_index != oracle.dex_method_index ||
        (observed.access_flags & kJavaModifiers) != (oracle.java_modifiers & kJavaModifiers))
      return fail("Executable source fields do not match named/DEX/reflection oracles");
  }
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::executable_mirror

#pragma once
#include <string>

namespace artdcevm::jni_access {
enum class DecoderModel { kLegacyAny, kGlobalOnly };
enum class PeerModel { kConst, kNonconst };
struct RuntimeCandidate {
  const char* id;
  DecoderModel decoder;
  PeerModel peer;
};
using SymbolResolver = void* (*)(void* context, const char* name);

// Selects release JNI ABI candidates from exact named symbols in the loaded
// runtime. No device identity, Build ID or SDK version participates in dispatch.
// Global decoding is preferred because the probe owns a strong global reference.
// A candidate does not authorize private layouts or a backend: the caller must
// calibrate decoder/peer identity inside an owned, live ART Runnable callback.
// Missing or ambiguous peer models fail without changing *out. No ART functions
// are called and no hooks are installed by this selection step.
bool SelectRuntimeCandidate(SymbolResolver resolver, void* context,
                            RuntimeCandidate* out, std::string* error);
}  // namespace artdcevm::jni_access

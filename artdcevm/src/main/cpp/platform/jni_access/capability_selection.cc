#include "platform/jni_access/capability_selection.h"

namespace artdcevm::jni_access {
bool SelectRuntimeCandidate(SymbolResolver resolver, void* context,
                            RuntimeCandidate* out, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!out) return fail("Missing JNI runtime candidate output");
  if (!resolver) return fail("Missing JNI runtime symbol resolver");
  const bool global = resolver(context, "_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject") != nullptr;
  const bool legacy = !global &&
      resolver(context, "_ZNK3art6Thread13DecodeJObjectEP8_jobject") != nullptr;
  const bool peer_const = resolver(context, "_ZNK3art6Thread22GetPeerFromOtherThreadEv") != nullptr;
  const bool peer_nonconst = resolver(context, "_ZN3art6Thread22GetPeerFromOtherThreadEv") != nullptr;
  if (!legacy && !global) return fail("Missing named JNI reference decoder");
  if (!peer_const && !peer_nonconst) return fail("Missing named current-thread peer accessor");
  if (peer_const && peer_nonconst) return fail("Ambiguous named current-thread peer models");
  const char* id = global
      ? (peer_const ? "global-decoder/const-peer" : "global-decoder/nonconst-peer")
      : (peer_const ? "legacy-decoder/const-peer" : "legacy-decoder/nonconst-peer");
  *out = {id, global ? DecoderModel::kGlobalOnly : DecoderModel::kLegacyAny,
          peer_const ? PeerModel::kConst : PeerModel::kNonconst};
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::jni_access

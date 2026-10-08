#include "platform/jni_access/captured_profile.h"
#include <cstring>
namespace artdcevm::jni_access {
namespace {
struct Entry { const char* build_id; CapturedProfile profile; };
// Exact libart note identities; release JNI declarations only. Source SDK tags
// label evidence, never dispatch. Pixel shiba's captured peer is nonconst even
// though Android14 r1's source still declares const (Mainline divergence).
constexpr Entry kCaptured[] = {
  {"6d01a26aa5bb297b773f6354dac69d75", {"redfin-30", DecoderModel::kLegacyAny, PeerModel::kConst}},
  {"e6c658201ef1ec3760112fa1b838ab2c", {"oriole-31", DecoderModel::kLegacyAny, PeerModel::kConst}},
  {"2b417e2566f5eb686666666b6ee952ea", {"bluejay-32", DecoderModel::kLegacyAny, PeerModel::kConst}},
  {"289d75599f6112d5757113220599e90b", {"panther-33", DecoderModel::kLegacyAny, PeerModel::kConst}},
  {"97fdb979efb7d2b596fa4fceabaad95b", {"b0q-33", DecoderModel::kLegacyAny, PeerModel::kConst}},
  {"02bec5940be704b863f6514fc7d81c41", {"shiba-34", DecoderModel::kGlobalOnly, PeerModel::kNonconst}},
  {"c35c9ebf7bb06435e4b31977d87bd5d5", {"husky-35", DecoderModel::kGlobalOnly, PeerModel::kNonconst}},
  {"be34fbe63ff357beb403f9cb39923ea7", {"frankel-36", DecoderModel::kGlobalOnly, PeerModel::kNonconst}},
  {"1f156fc62660d075a8d675db850b95d5", {"cubs-37", DecoderModel::kGlobalOnly, PeerModel::kNonconst}},
  {"14fc77b24db0b351b919c97009dc86bf", {"xiaomi-cas-33", DecoderModel::kLegacyAny, PeerModel::kConst}},
};
}
bool MatchCapturedProfile(const LoadedModule& art, CapturedProfile* out, std::string* error) {
  const auto fail = [&](const char* message) { if (error) *error = message; return false; };
  if (!out) return fail("Missing captured JNI profile output");
  const char* base = std::strrchr(art.path.c_str(), '/');
  base = base ? base + 1 : art.path.c_str();
  if (std::strcmp(base, "libart.so") != 0 || art.build_id.empty())
    return fail("Missing loaded libart identity for captured JNI probe");
  for (const auto& entry : kCaptured) {
    if (art.build_id == entry.build_id) {
      *out = entry.profile;
      if (error) error->clear();
      return true;
    }
  }
  return fail("Loaded libart Build ID is not in the captured JNI probe catalog");
}
}  // namespace artdcevm::jni_access

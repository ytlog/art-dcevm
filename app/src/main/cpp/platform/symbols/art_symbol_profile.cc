#include "platform/symbols/art_symbol_profile.h"
#include "platform/symbols/android13/xiaomi/cas_tkq1_221114_001.h"

namespace artdcevm::symbols {
namespace {
constexpr ArtSymbolProfile kProfiles[] = {
    {33, "xiaomi", "cas_tkq1_221114_001", android13::xiaomi::cas::kArtBuildId,
     android13::xiaomi::cas::kJvmtiBuildId, android13::xiaomi::cas::kCompilerBuildId},
};
}

const ArtSymbolProfile* FindProfile(int sdk, std::string_view art_build_id,
                                    std::string_view jvmti_build_id) {
  for (const auto& profile : kProfiles) {
    if (sdk == profile.sdk && art_build_id == profile.art_build_id &&
        jvmti_build_id == profile.jvmti_build_id) return &profile;
  }
  return nullptr;
}

void* FindSymbol(HookEngine& hooks, NamedSymbol symbol) {
  return hooks.FindSymbol(LibraryName(symbol.library), symbol.name);
}

}  // namespace artdcevm::symbols

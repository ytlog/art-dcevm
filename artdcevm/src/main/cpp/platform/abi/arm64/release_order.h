#pragma once
#include <vector>

namespace artdcevm::abi::arm64 {
constexpr int PlatformRelease(int api) {
  return api <= 30 ? 11 : api <= 32 ? 12 : api - 20;
}
// ART modules cannot predate the platform. Try its own release, then newer ones.
inline std::vector<int> CandidateReleases(int api) {
  std::vector<int> releases;
  for (int release = PlatformRelease(api); release <= 17; ++release)
    if (release >= 11) releases.push_back(release);
  return releases;
}
}  // namespace artdcevm::abi::arm64

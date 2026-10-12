#pragma once
#include <array>

namespace artdcevm::method_retirement {
using BuildId = std::array<unsigned char, 16>;
// Whether the loaded libart and libopenjdkjvmti carry exactly these GNU build
// IDs. Selects which fixed declarations apply; not an address resolver or a
// security boundary.
bool LoadedBuildIdsMatch(const BuildId& art, const BuildId& jvmti);
}

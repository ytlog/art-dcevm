#pragma once
#include <string>

namespace artdcevm::method_retirement {
// Match loaded ELF provenance before selecting the fixed Android 16 declarations.
// This is a compatibility profile, not an address resolver or security boundary.
bool MatchesRuntimeProfile(std::string* reason);
}

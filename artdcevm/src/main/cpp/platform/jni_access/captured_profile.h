#pragma once
#include "support/elf_modules.h"
#include <string>
namespace artdcevm::jni_access {
enum class DecoderModel { kLegacyAny, kGlobalOnly };
enum class PeerModel { kConst, kNonconst };
struct CapturedProfile { const char* id; DecoderModel decoder; PeerModel peer; };
// Captured ARM64 libart identity for the isolated debug JNI probe only. This
// catalog does not authorize a private backend, Thread layout or SDK family.
// Every entry has the exact decoder/peer and C2/D2 suspend names in its ELF.
// Runtime symbol lookup and owned behavioral calibration remain mandatory.
// Failure preserves *out. No modules are loaded, hooks installed or ART called.
bool MatchCapturedProfile(const LoadedModule& art, CapturedProfile* out, std::string* error);
}  // namespace artdcevm::jni_access

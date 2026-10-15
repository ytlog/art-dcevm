#pragma once
#include <string>
#include <vector>
#include "platform/common/art_hook.h"

// ART verifies the new DEX before the commit. Since Android 13 its verifier
// resolves the class's own descriptor through the class loader, which still
// yields the old class (old superclass), so a call to the new superclass's
// constructor fails verification. While ART verifies a target whose
// transaction Requirements ask for it, this thread's FindClass answers that
// descriptor with the new class object, which the holder keeps rooted. For an
// interface group, interface_redefinition answers with the group's
// replacements instead.
namespace artdcevm::verification_redirect {

// Resolve the CheckVerification and FindClass hooks by name. False with
// `failure` when CheckVerification is missing; `hooks` is then unchanged.
bool Prepare(std::vector<Hook>* hooks, std::string* failure);
// Either FindClass spelling is hooked, so verification can be redirected.
bool ResolvesClasses();
}  // namespace artdcevm::verification_redirect

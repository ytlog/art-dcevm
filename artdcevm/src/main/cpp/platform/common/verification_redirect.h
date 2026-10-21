#pragma once
#include <string>
#include <vector>
#include <jvmti.h>
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_hook.h"

// ART verifies the new DEX before commit. Its self descriptor normally resolves
// to the published class, whose methods or superclass can differ from the new
// definition (for example, a restored virtual call still resolves private).
// While ART verifies a target that requests redirection, FindClass answers its
// self descriptor with the rooted replacement. Interface groups use their own
// replacement lookup for every affected member.
namespace artdcevm::interface_redefinition { struct Backend; }
namespace artdcevm::verification_redirect {

// The interface backend Install published, or null; the only way to set it.
void BindInterfaceBackend(interface_redefinition::Backend*);
// Allocate the verification Part while the standard entry is still Native.
void Begin();
// Resolve the CheckVerification and FindClass hooks by name. False with
// `failure` when CheckVerification is missing; `hooks` is then unchanged.
bool Prepare(std::vector<Hook>* hooks, std::string* failure);
// Either FindClass spelling is hooked, so verification can be redirected.
bool ResolvesClasses();
// Old array components can be translated at ART's verifier register cache.
// This does not allocate private array mirrors or alter published components.
bool ResolvesRegisterTypes();
}  // namespace artdcevm::verification_redirect

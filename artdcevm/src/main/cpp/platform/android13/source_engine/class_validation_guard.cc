#include "ti_redefine.h"
#include "platform/android13/class_validation_abi.h"
namespace {
static_assert(art::kAccValidClassFlags == artdcevm::android13::kValidationClassFlags);
static_assert(art::kAccValidInterfaceFlags == artdcevm::android13::kValidationInterfaceFlags);
artdcevm::android13::ClassValidation validate = nullptr;
}
extern "C" void ArtDcevmSourceConfigureClassValidation(artdcevm::android13::ClassValidation callback) {
  validate = callback;
}
extern "C" bool ArtDcevmSourceCheckClass(void* definition) { return validate(definition); }

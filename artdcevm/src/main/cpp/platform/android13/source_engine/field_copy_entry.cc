#include "platform/android13/field_copy_abi.h"

namespace {
artdcevm::android13::EvolutionFieldCopy copy = nullptr;
}
extern "C" void ArtDcevmSourceConfigureFieldCopy(
    artdcevm::android13::EvolutionFieldCopy callback) {
  copy = callback;
}
extern "C" void ArtDcevmSourceCopyAndClearFields(bool is_static, void* target,
    void* target_class, void* source, void* source_class) {
  copy(is_static, target, target_class, source, source_class);
}

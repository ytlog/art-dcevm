#pragma once
#include "platform/android13/field_copy_abi.h"
#include <string>
namespace artdcevm::android13 {
// ART has already allocated replacement objects and suspended mutation. This
// operation performs no allocation, creates no JNI IDs, and cannot fail.
void CopyAndClearEvolutionFields(const FieldCopyAccess&, bool is_static,
    void* target, void* target_class, void* source, void* source_class) noexcept;
}

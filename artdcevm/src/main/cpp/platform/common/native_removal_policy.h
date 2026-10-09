#pragma once
#include "evolution/class_schema.h"
namespace artdcevm::native_owned_removal {
// Public JVMTI modifiers and DEX declared-synchronized bits are normalized to
// the same Java flags. This deliberately permits removals only, with hierarchy
// and remaining declarations unchanged; ART still verifies incoming code.
// require_removal=false admits an unchanged declaration only as a batch item;
// the source entry separately requires at least one actual removal in the batch.
bool ValidateRemoval(const evolution::ClassSchema& live,
                     const evolution::ClassSchema& incoming,bool allow_fields,std::string* error,bool require_removal=true);
}

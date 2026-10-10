#pragma once
#include "api/hook_engine.h"
#include "platform/common/field_copy.h"
namespace artdcevm::android13 {
bool ConfigureFieldCopyMetadata(HookEngine&, std::string*);
field_copy::CopyClassView ReadCopyClass(void*, bool);
field_copy::CopyFieldView ReadCopyField(void*, bool, size_t);
}

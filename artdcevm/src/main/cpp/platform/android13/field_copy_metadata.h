#pragma once
#include "api/hook_engine.h"
#include "platform/android13/field_copy_abi.h"
namespace artdcevm::android13 {
bool ConfigureFieldCopyMetadata(HookEngine&, std::string*);
CopyClassView ReadCopyClass(void*, bool);
CopyFieldView ReadCopyField(void*, bool, size_t);
}

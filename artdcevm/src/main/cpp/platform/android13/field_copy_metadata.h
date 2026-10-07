#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include "platform/android13/field_copy_abi.h"
namespace artdcevm::android13 {
bool ConfigureFieldCopyMetadata(HookEngine&, std::string*);
CopyClassView ReadCopyClass(void*, bool);
CopyFieldView ReadCopyField(void*, bool, size_t);
using CopyClassReader = CopyClassView (*)(void*, bool);
using CopyFieldReader = CopyFieldView (*)(void*, bool, size_t);
using ProbeFieldCopyMetadata = int (*)(JNIEnv*, jobject, CopyClassReader, CopyFieldReader);
}

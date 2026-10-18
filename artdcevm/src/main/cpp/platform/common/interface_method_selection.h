#pragma once
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include <jni.h>
#include <string>
#include <vector>

namespace artdcevm::interface_method_selection {
using LinkMethodsFunction = bool (*)(void*, void*, abi::arm64::Handle, abi::arm64::Handle, bool*, void**);
using LinkClassFunction = bool (*)(void*, void*, const char*, abi::arm64::Handle, abi::arm64::Handle, abi::arm64::Handle*);
bool Prepare(JavaVM*, JNIEnv*, std::vector<Hook>*, std::string*);
bool Available();
bool LinkingHelper();
bool LinkClass(LinkClassFunction, void*, void*, const char*, abi::arm64::Handle, abi::arm64::Handle, abi::arm64::Handle*) noexcept;
bool LinkMethods(LinkMethodsFunction, void*, void*, abi::arm64::Handle, abi::arm64::Handle, bool*, void**) noexcept;
void Complete(bool success) noexcept;
}  // namespace artdcevm::interface_method_selection

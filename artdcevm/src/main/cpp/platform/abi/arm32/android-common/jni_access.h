#pragma once
#include "platform/abi/common/jni_access.h"
namespace artdcevm::abi::arm32::android_common {
using common::RunnableJniScope;
using common::JniEnvironmentPrefix;
using common::FromReflectedMethod;
using common::kFromReflectedMethod;
#if defined(__arm__)
static_assert(sizeof(void*)==4 && sizeof(RunnableJniScope)==12);
static_assert(sizeof(JniEnvironmentPrefix)==12);
#endif
}

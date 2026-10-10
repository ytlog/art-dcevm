#pragma once
#include "platform/abi/common/jni_access.h"
namespace artdcevm::abi::arm64::android_common {
using common::RunnableJniScope;
using common::JniEnvironmentPrefix;
using common::FromReflectedMethod;
using common::kFromReflectedMethod;
static_assert(sizeof(RunnableJniScope)==24 && offsetof(RunnableJniScope,env)==8);
static_assert(sizeof(JniEnvironmentPrefix)==24 && offsetof(JniEnvironmentPrefix,self)==8);
}

#pragma once
#include "evolution/class_schema.h"
#include "jvmti.h"
namespace artdcevm::android13 {
// Reads the live class, so a second patch plans against the already evolved definition.
int ReadLiveSchema(jvmtiEnv* jvmti, JNIEnv* jni, jclass klass,
                   evolution::ClassSchema* output, std::string* error);
}

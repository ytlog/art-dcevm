#pragma once
#include "evolution/class_schema.h"
#include "jvmti.h"
// Version-independent public JVMTI/JNI declaration snapshots. No private ART
// layout, CPU declaration or version dispatch belongs in this module.
namespace artdcevm::live_schema {
int ReadLiveMethods(jvmtiEnv*, jclass, std::vector<evolution::Member>*, std::string*);
int ReadLiveClassMetadata(jvmtiEnv*, JNIEnv*, jclass, evolution::ClassSchema*, std::string*);
// Reads the live class, so a second patch plans against the already evolved definition.
int ReadLiveSchema(jvmtiEnv* jvmti, JNIEnv* jni, jclass klass,
                   evolution::ClassSchema* output, std::string* error);
int ReadLiveFields(jvmtiEnv* jvmti, jclass klass,
                   std::vector<evolution::Member>* output, std::string* error);
}

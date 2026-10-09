#pragma once
#include <jni.h>
#include "platform/common/jni_receiver_metadata.h"
namespace artdcevm::arm32::jni_receiver_metadata {
// current_self must be supplied by independently observed named ART Thread
// access, and current_vm by JNI GetJavaVM, on the current owning native thread.
// No borrowed scope is manufactured and no thread transition is performed.
bool ReadCurrentEnvironment(JNIEnv* environment,size_t mapped_extent,
                            void* current_self,JavaVM* current_vm,
                            artdcevm::jni_receiver_metadata::Receiver* output);
}

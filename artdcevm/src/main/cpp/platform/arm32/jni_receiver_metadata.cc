#include "platform/arm32/jni_receiver_metadata.h"
#include "platform/abi/arm32/android-common/jni_access.h"
#include "platform/abi/arm32/android-common/suspend_all.h"
#include "platform/common/arm32_method_metadata.h"
static_assert(sizeof(void*)==4, "ARM32 receiver must compile for actual 32-bit ABI");
namespace artdcevm::arm32::jni_receiver_metadata {
bool ReadCurrentEnvironment(JNIEnv* environment,size_t mapped_extent,
                            void* current_self,JavaVM* current_vm,
                            artdcevm::jni_receiver_metadata::Receiver* output) {
  return artdcevm::jni_receiver_metadata::ReadMatchedPrefix(4,environment,
      mapped_extent,reinterpret_cast<uintptr_t>(current_self),
      reinterpret_cast<uintptr_t>(current_vm),output);
}
}

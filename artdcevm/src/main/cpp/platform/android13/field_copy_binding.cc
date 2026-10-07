#include "platform/android13/field_copy_binding.h"
#include "platform/android13/field_copy.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_storage.h"
#include "platform/android13/heap_reference.h"
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
FieldCopyAccess access{};
}
void CopyEvolutionFields(bool is_static, void* target, void* target_class, void* source, void* source_class) {
  CopyAndClearEvolutionFields(access, is_static, target, target_class, source, source_class);
}
bool ConfigureEvolutionFieldCopy(HookEngine& hooks, void* implementation, JNIEnv* jni,
    jobject calibration, std::string* error) {
  using ProbeLayout = int (*)();
  using ProbeStorage = int (*)(JNIEnv*, jobject, const FieldCopyAccess*);
  auto layout = reinterpret_cast<ProbeLayout>(
      dlsym(implementation, "ArtDcevmSourceProbeHeapReferenceLayout"));
  auto metadata = reinterpret_cast<ProbeFieldCopyMetadata>(
      dlsym(implementation, "ArtDcevmSourceProbeFieldCopyMetadata"));
  auto storage = reinterpret_cast<ProbeStorage>(
      dlsym(implementation, "ArtDcevmSourceProbeFieldCopyStorage"));
  if (!layout || !metadata || !storage) {
    *error = "Missing field-copy source declaration calibration";
    return false;
  }
  if (layout() != 0 || !ConfigureHeapReferenceReader(hooks, error) ||
      !ConfigureFieldCopyMetadata(hooks, error) || !ConfigureFieldCopyStorage(hooks, error)) {
    if (error->empty()) *error = "Field-copy release Baker profile is unsupported";
    return false;
  }
  access = {ReadCopyClass, ReadCopyField, ReadCopyValue, WriteCopyValue,
            TransferCopyMonitor, CopyWriteBarrier};
  int code = metadata(jni, calibration, ReadCopyClass, ReadCopyField);
  if (!code && !jni->ExceptionCheck()) code = storage(jni, calibration, &access);
  if (code || jni->ExceptionCheck()) {
    *error = "Field-copy declaration calibration failed at step " + std::to_string(code);
    return false;
  }
  return true;
}
}

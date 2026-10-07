#include "platform/android13/field_copy_binding.h"
#include "platform/android13/field_copy.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_storage.h"
#include "platform/android13/heap_reference.h"

namespace artdcevm::android13 {
namespace {
FieldCopyAccess access{};
}
void CopyEvolutionFields(bool is_static, void* target, void* target_class, void* source, void* source_class) {
  CopyAndClearEvolutionFields(access, is_static, target, target_class, source, source_class);
}
bool ConfigureEvolutionFieldCopy(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Field copy requires a matched device profile";return false;}
  if (!ConfigureHeapReferenceReader(hooks, error) ||
      !ConfigureFieldCopyMetadata(hooks, error) || !ConfigureFieldCopyStorage(hooks, error)) return false;
  access = {ReadCopyClass, ReadCopyField, ReadCopyValue, WriteCopyValue,
            TransferCopyMonitor, CopyWriteBarrier};
  return true;
}
}

#include "platform/android13/field_copy_storage.h"
#include "platform/android13/field_copy_storage_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/common/field_storage.h"
namespace artdcevm::android13 {
namespace {
field_copy_storage_abi::CurrentThread current_thread=nullptr;
field_storage::Field Field(void* raw) {field_storage::Field field;std::memcpy(&field,raw,sizeof(field));return field;}
}
bool ConfigureFieldCopyStorage(HookEngine& hooks,std::string* error) {
  auto thread=reinterpret_cast<field_copy_storage_abi::CurrentThread>(
      hooks.FindSymbol("libart.so",field_copy_storage_abi::kCurrentThread));
  if(!thread){*error="Missing named ART current-thread function for field-copy barrier";return false;}
  if(!ConfigureHeapReferenceReader(hooks,error))return false;
  current_thread=thread;return true;
}
uint64_t ReadCopyValue(void* object,void* field,char type) {
  return field_storage::Read(object,Field(field),type,ReadHeapReference);
}
void WriteCopyValue(void* object,void* field,char type,uint64_t value) {
  field_storage::Write(object,Field(field),type,value,CopyWriteBarrier);
}
void TransferCopyMonitor(void* target,void* source) {
  field_storage::TransferMonitor(target,source,field_copy_storage_abi::kMonitorOffset,
      field_copy_storage_abi::kDefaultLockWord);
}
void CopyWriteBarrier(void* object) {
  using namespace field_copy_storage_abi;
  field_storage::MarkCard(object,current_thread,kThreadCardTableOffset,kCardShift,kCardDirty);
}
}

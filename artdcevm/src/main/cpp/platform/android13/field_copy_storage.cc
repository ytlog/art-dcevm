#include "platform/android13/field_copy_storage.h"
#include "platform/android13/field_copy_storage_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include <cstdlib>
#include <cstring>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Field-copy packing requires little endian");

namespace artdcevm::android13 {
namespace {
field_copy_storage_abi::CurrentThread current_thread = nullptr;
field_copy_abi::ArtField Field(void* raw) {
  field_copy_abi::ArtField field;
  std::memcpy(&field, raw, sizeof(field));
  return field;
}
template<class T> uint64_t ReadPrimitive(void* object, uint32_t offset, bool is_volatile) {
  const auto* address = reinterpret_cast<const T*>(static_cast<unsigned char*>(object) + offset);
  const T value = __atomic_load_n(address, is_volatile ? __ATOMIC_SEQ_CST : __ATOMIC_RELAXED);
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}
template<class T> void WritePrimitive(void* object, uint32_t offset, bool is_volatile, uint64_t bits) {
  T value;
  std::memcpy(&value, &bits, sizeof(value));
  auto* address = reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
  __atomic_store_n(address, value, is_volatile ? __ATOMIC_SEQ_CST : __ATOMIC_RELAXED);
}
}  // namespace
bool ConfigureFieldCopyStorage(HookEngine& hooks, std::string* error) {
  auto thread = reinterpret_cast<field_copy_storage_abi::CurrentThread>(
      hooks.FindSymbol("libart.so", field_copy_storage_abi::kCurrentThread));
  if (!thread) { *error = "Missing named ART current-thread function for field-copy barrier"; return false; }
  if (!ConfigureHeapReferenceReader(hooks, error)) return false;
  current_thread = thread;
  return true;
}
uint64_t ReadCopyValue(void* object, void* raw_field, char type) {
  const auto field = Field(raw_field);
  const bool is_volatile = (field.access_flags & field_copy_storage_abi::kVolatileFlag) != 0;
  switch (type) {
    case 'L': case '[':
      return reinterpret_cast<uintptr_t>(ReadHeapReference(object, field.offset, is_volatile));
    // The old Pack<T> copied only sizeof(T) bytes and zeroed the upper bits,
    // including signed byte/short/int. Floating-point values retain raw bits.
    case 'Z': case 'B': return ReadPrimitive<uint8_t>(object, field.offset, is_volatile);
    case 'C': case 'S': return ReadPrimitive<uint16_t>(object, field.offset, is_volatile);
    case 'I': case 'F': return ReadPrimitive<uint32_t>(object, field.offset, is_volatile);
    case 'J': case 'D': return ReadPrimitive<uint64_t>(object, field.offset, is_volatile);
    default: std::abort();
  }
}
void WriteCopyValue(void* object, void* raw_field, char type, uint64_t bits) {
  const auto field = Field(raw_field);
  const bool is_volatile = (field.access_flags & field_copy_storage_abi::kVolatileFlag) != 0;
  switch (type) {
    case 'L': case '[':
      // Android 13's validated release profile uses unpoisoned HeapReference32.
      WritePrimitive<uint32_t>(object, field.offset, is_volatile, bits);
      if (bits != 0) CopyWriteBarrier(object);
      return;
    case 'Z': case 'B': WritePrimitive<uint8_t>(object, field.offset, is_volatile, bits); return;
    case 'C': case 'S': WritePrimitive<uint16_t>(object, field.offset, is_volatile, bits); return;
    case 'I': case 'F': WritePrimitive<uint32_t>(object, field.offset, is_volatile, bits); return;
    case 'J': case 'D': WritePrimitive<uint64_t>(object, field.offset, is_volatile, bits); return;
    default: std::abort();
  }
}
void TransferCopyMonitor(void* target, void* source) {
  using namespace field_copy_storage_abi;
  const uint64_t monitor = ReadPrimitive<uint32_t>(source, kMonitorOffset, false);
  WritePrimitive<uint32_t>(target, kMonitorOffset, false, monitor);
  WritePrimitive<uint32_t>(source, kMonitorOffset, false, kDefaultLockWord);
}
void CopyWriteBarrier(void* object) {
  using namespace field_copy_storage_abi;
  auto* thread = static_cast<unsigned char*>(current_thread());
  uintptr_t biased;
  std::memcpy(&biased, thread + kThreadCardTableOffset, sizeof(biased));
  // The thread's source-declared card_table is the same biased pointer used
  // by ART compiled stores. Mark the owner card, independent of field offset.
  auto* card = reinterpret_cast<uint8_t*>(biased + (reinterpret_cast<uintptr_t>(object) >> kCardShift));
  __atomic_store_n(card, kCardDirty, __ATOMIC_RELAXED);
}
}

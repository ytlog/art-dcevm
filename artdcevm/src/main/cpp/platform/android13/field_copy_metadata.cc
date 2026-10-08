#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/common/dex_file_metadata.h"
#include <cstdlib>
#include <cstring>
namespace artdcevm::android13 {
namespace {
using FieldText = const char* (*)(void*);
FieldText name = nullptr, descriptor = nullptr;

void Require(bool condition) { if (!condition) std::abort(); }
void Range(size_t offset, size_t width, size_t size) { Require(offset <= size && width <= size - offset); }
uint32_t U32(const unsigned char* region, size_t size, size_t offset) {
  Range(offset, 4, size);
  return region[offset] | uint32_t{region[offset + 1]} << 8 |
      uint32_t{region[offset + 2]} << 16 | uint32_t{region[offset + 3]} << 24;
}
size_t Id(const unsigned char* begin, size_t size, size_t header, uint32_t index) {
  const uint32_t count = U32(begin, size, header), offset = U32(begin, size, header + 4);
  Require(index < count && offset >= 0x70 && offset % 4 == 0);
  Range(offset, size_t{count} * 4, size);
  return offset + size_t{index} * 4;
}
const char* ClassDescriptor(void* klass) {
  using namespace validation_abi;
  // Class::GetDexFile deliberately reads the immutable DexCache link without
  // a read barrier. Both cache copies point to the same native DEX mapping.
  class_metadata::Identity identity;
  Require(class_metadata::ReadIdentity(33,klass,sizeof(ClassPrefix),&identity) && identity.dex_type_index>=0);
  auto* cache=reinterpret_cast<const void*>(static_cast<uintptr_t>(identity.dex_cache));
  evolution::MappedDexMetadata mapping;
  Require(dex_file_metadata::ReadCache(33,cache,sizeof(DexCachePrefix),sizeof(DexMappingPrefix),0,&mapping,nullptr));
  const uint32_t type=static_cast<uint32_t>(identity.dex_type_index);
  const uint32_t string = U32(mapping.begin, mapping.size, Id(mapping.begin, mapping.size, 64, type));
  size_t at = U32(mapping.begin, mapping.size, Id(mapping.begin, mapping.size, 56, string));
  // Skip the bounded UTF-16 length ULEB; the mapped DEX is already verified.
  bool complete = false;
  for (unsigned i = 0; i < 5; ++i) {
    Range(at, 1, mapping.data_size); unsigned byte = mapping.data_begin[at++];
    if (i == 4) Require((byte & 0xf0) == 0);
    if (!(byte & 0x80)) { complete = true; break; }
  }
  Require(complete);
  Range(at, 1, mapping.data_size);
  Require(std::memchr(mapping.data_begin + at, 0, mapping.data_size - at) != nullptr);
  return reinterpret_cast<const char*>(mapping.data_begin + at);
}
field_array::View FieldArray(void* klass, bool is_static) {
  field_array::View view;
  Require(field_array::ReadTrustedClass(33, klass, sizeof(validation_abi::ClassPrefix), is_static, &view));
  return view;
}
}
CopyClassView ReadCopyClass(void* klass, bool is_static) {
  using namespace validation_abi;
  Require(klass != nullptr);
  auto* parent = ReadHeapReference(klass, offsetof(ClassPrefix, super_class));
  auto* loader = ReadHeapReference(klass, offsetof(ClassPrefix, class_loader));
  const auto fields = FieldArray(klass, is_static);
  return {parent, loader, parent == nullptr ? "Ljava/lang/Object;" : ClassDescriptor(klass),
          fields.selected_count,
          parent == nullptr};
}
CopyFieldView ReadCopyField(void* klass, bool is_static, size_t index) {
  const auto fields = FieldArray(klass, is_static);
  Require(name != nullptr && descriptor != nullptr && index < fields.selected_count);
  auto* field = fields.AtSelected(static_cast<uint32_t>(index));
  Require(field != nullptr);
  const char* field_name = name(field);
  const char* field_type = descriptor(field);
  Require(field_name != nullptr && field_type != nullptr);
  return {field, field_name, field_type};
}
bool ConfigureFieldCopyMetadata(HookEngine& hooks, std::string* error) {
  auto get_name = reinterpret_cast<FieldText>(hooks.FindSymbol("libart.so", "_ZN3art8ArtField7GetNameEv"));
  auto get_type = reinterpret_cast<FieldText>(hooks.FindSymbol("libart.so", "_ZN3art8ArtField17GetTypeDescriptorEv"));
  if (!get_name || !get_type) { *error = "Missing named ART field name or type accessor"; return false; }
  name = get_name; descriptor = get_type;
  return true;
}
}

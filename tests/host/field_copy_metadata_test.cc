#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace artdcevm;
namespace {
using RawClass = android13::validation_abi::ClassPrefix;
using RawField = android13::field_copy_abi::ArtField;
using RawDex = android13::validation_abi::DexMappingPrefix;
struct Fields { uint32_t count = 2; RawField values[2]{}; };
RawClass root{};
int name_calls = 0, type_calls = 0, heap_reads = 0;
void* canonical_loader = reinterpret_cast<void*>(0x100);
uint32_t U32(const std::vector<unsigned char>& bytes, size_t at) { uint32_t value; std::memcpy(&value, bytes.data() + at, 4); return value; }
void Put(std::vector<unsigned char>& bytes, size_t at, uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); }
const char* Name(void* value) {
  ++name_calls; auto* field = static_cast<RawField*>(value);
  return field->dex_field_index == 1 ? "wide" : "value";
}
const char* Type(void* value) {
  ++type_calls; auto* field = static_cast<RawField*>(value);
  return field->dex_field_index == 1 ? "J" : "I";
}
class Hooks : public HookEngine {
 public:
  bool omit_type = false;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* name) override {
    assert(std::string(library) == "libart.so");
    if (std::string(name) == "_ZN3art8ArtField7GetNameEv") return reinterpret_cast<void*>(Name);
    assert(std::string(name) == "_ZN3art8ArtField17GetTypeDescriptorEv");
    return omit_type ? nullptr : reinterpret_cast<void*>(Type);
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
template<class Fn> void Reject(Fn action) {
  pid_t child = fork(); assert(child >= 0);
  if (child == 0) { action(); _exit(0); }
  int status = 0; assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
}
namespace artdcevm::android13 {
void* ReadHeapReference(void* object, size_t offset, bool is_volatile) {
  ++heap_reads; assert(!is_volatile);
  assert(offset == offsetof(validation_abi::ClassPrefix, class_loader) ||
         offset == offsetof(validation_abi::ClassPrefix, super_class));
  uint32_t ref; std::memcpy(&ref, static_cast<unsigned char*>(object) + offset, 4);
  if (ref == 0) return nullptr;
  if (ref == 1) return &root;
  // Different compressed references normalize to the same loader identity.
  assert(ref == 2 || ref == 3); return canonical_loader;
}
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary); assert(input.good());
  std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  void* memory = mmap(reinterpret_cast<void*>(0x20000000), 4096, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(memory != MAP_FAILED && reinterpret_cast<uintptr_t>(memory) <= UINT32_MAX);
  auto* cache = static_cast<android13::validation_abi::DexCachePrefix*>(memory);
  RawDex dex{nullptr, bytes.data(), bytes.size(), bytes.data(), bytes.size()};
  cache->dex_file = reinterpret_cast<uintptr_t>(&dex);
  Fields instance, statics;
  instance.values[0].dex_field_index = 0; instance.values[1].dex_field_index = 1;
  statics.values[0].dex_field_index = 1; statics.count = 1;
  RawClass klass{};
  klass.dex_cache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory));
  klass.dex_type_index = static_cast<int32_t>(U32(bytes, U32(bytes, 100)));
  klass.class_loader = 2; klass.super_class = 1;
  klass.ifields = reinterpret_cast<uintptr_t>(&instance); klass.sfields = reinterpret_cast<uintptr_t>(&statics);
  Hooks hooks; std::string error;
  hooks.omit_type = true; assert(!android13::ConfigureFieldCopyMetadata(hooks, &error) && !error.empty());
  hooks.omit_type = false; assert(android13::ConfigureFieldCopyMetadata(hooks, &error));
  auto view = android13::ReadCopyClass(&klass, false);
  assert(view.parent == &root && view.loader == canonical_loader && view.field_count == 2 && !view.is_object);
  assert(std::string(view.descriptor) == "Lcom/github/ytlog/artdcevm/fixture/SchemaSubject;");
  assert(heap_reads == 2);
  // Copy metadata also visits loaded primitive/array classes; it must retain
  // their valid descriptors rather than imposing ordinary-class semantics.
  bool primitive = false;
  for(uint32_t i=0;i<U32(bytes,64);++i) {
    const uint32_t str=U32(bytes,U32(bytes,68)+size_t{i}*4);
    size_t at=U32(bytes,U32(bytes,60)+size_t{str}*4);
    while(bytes[at++]&0x80) {}
    auto other=klass;other.dex_type_index=static_cast<int32_t>(i);
    const auto copied=android13::ReadCopyClass(&other,false);
    assert(copied.descriptor==reinterpret_cast<const char*>(bytes.data()+at));
    primitive|=std::strcmp(copied.descriptor,"I")==0;
  }
  assert(primitive);

  auto alias = klass; alias.class_loader = 3;
  auto second = android13::ReadCopyClass(&alias, true);
  assert(second.loader == view.loader && second.field_count == 1);
  auto first_field = android13::ReadCopyField(&klass, false, 0);
  auto wide_field = android13::ReadCopyField(&klass, false, 1);
  auto static_field = android13::ReadCopyField(&klass, true, 0);
  assert(first_field.field == &instance.values[0] && std::strcmp(first_field.name, "value") == 0);
  assert(wide_field.field == &instance.values[1] && std::strcmp(wide_field.descriptor, "J") == 0);
  assert(static_field.field == &statics.values[0] && name_calls == 3 && type_calls == 3);
  auto terminal = android13::ReadCopyClass(&root, false);
  assert(terminal.is_object && terminal.parent == nullptr && terminal.loader == nullptr && terminal.field_count == 0);
  assert(std::strcmp(terminal.descriptor, "Ljava/lang/Object;") == 0); // No DEX required for terminal root.
  Reject([&] { android13::ReadCopyField(&klass, false, 2); });
  Reject([&] { android13::ReadCopyField(&root, false, 0); });
  Reject([&] { auto bad = klass; bad.dex_type_index = 0xffff; android13::ReadCopyClass(&bad, false); });
  Reject([&] { auto bad = klass; bad.dex_type_index = 0x10000; android13::ReadCopyClass(&bad, false); });
  Reject([&] { auto bad = klass; bad.dex_type_index = -1; android13::ReadCopyClass(&bad, false); });
  Reject([&] { auto bad = klass; bad.dex_cache = 0; android13::ReadCopyClass(&bad, false); });
  // CompactDex's variable strings can live outside the table mapping.
  const size_t data_start = U32(bytes, 108);
  std::vector<unsigned char> compact(bytes.begin(), bytes.begin() + data_start);
  compact.insert(compact.begin() + 0x70, 24, 0); std::memcpy(compact.data(), "cdex001\0", 8);
  Put(compact, 36, 0x88);
  for (size_t header : {size_t{56}, size_t{64}, size_t{72}, size_t{80}, size_t{88}, size_t{96}})
    if (U32(compact, header)) Put(compact, header + 4, U32(compact, header + 4) + 24);
  dex.begin = compact.data(); dex.size = compact.size();
  auto split = android13::ReadCopyClass(&klass, false);
  assert(std::strcmp(split.descriptor, view.descriptor) == 0);
  Reject([&] { dex.data_size = 1; android13::ReadCopyClass(&klass, false); });
  Reject([&] { Put(compact, 68, UINT32_MAX); android13::ReadCopyClass(&klass, false); });
  // A later 041 header uses ID tables relative to the whole container.
  auto container = bytes;
  const size_t header_offset = (container.size()+3)&~size_t{3};
  container.resize(header_offset);
  container.insert(container.end(),bytes.begin(),bytes.begin()+data_start);
  container.insert(container.begin()+header_offset+0x70,8,0);
  auto header_put = [&](size_t off,uint32_t value) { Put(container,header_offset+off,value); };
  std::memcpy(container.data()+header_offset+4,"041",3);
  header_put(36,0x78);header_put(32,container.size()-header_offset);
  header_put(104,0);header_put(108,0);
  header_put(0x70,container.size());header_put(0x74,header_offset);
  for(size_t off:{size_t{56},size_t{64},size_t{72},size_t{80},size_t{88},size_t{96}})
    if(U32(bytes,off))header_put(off+4,header_offset+U32(bytes,off+4)+8);
  dex.begin=container.data()+header_offset;dex.size=container.size()-header_offset;
  dex.data_begin=container.data();dex.data_size=container.size();
  auto later=android13::ReadCopyClass(&klass,false);
  assert(std::strcmp(later.descriptor,view.descriptor)==0);
  const uint32_t string_index=U32(bytes,U32(bytes,68)+static_cast<size_t>(klass.dex_type_index)*4);
  const size_t string_data=U32(bytes,U32(bytes,60)+size_t{string_index}*4);
  Reject([&] { container[string_data]^=1; android13::ReadCopyClass(&klass,false); });
  assert(munmap(memory, 4096) == 0);
  std::cout << "field-copy metadata: named accessors, checked field stride/count, normalized loader identities, parent/root, DEX/shared CompactDex/later041 descriptors, loaded primitive/array types, full signed type indices and invalid ranges passed\n";
}

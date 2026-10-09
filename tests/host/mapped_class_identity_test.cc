#include "evolution/class_schema.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
static bool forbid_allocation = false;
void* operator new(size_t count) {
  if (forbid_allocation) std::abort();
  if (void* value = std::malloc(count ? count : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
using namespace artdcevm::evolution;
using Bytes = std::array<unsigned char, 1024>;
static void Put(unsigned char* bytes, size_t offset, uint32_t value) {
  std::memcpy(bytes + offset, &value, 4);
}
static void Fixture(unsigned char* header, unsigned char* ids,
                    unsigned char* data, const char* magic, uint32_t header_size) {
  std::memcpy(header, magic, 8);
  Put(header, 32, 512); Put(header, 36, header_size); Put(header, 40, 0x12345678);
  Put(header, 56, 2); Put(header, 60, 144);
  Put(header, 64, 2); Put(header, 68, 152);
  Put(header, 96, 2); Put(header, 100, 160);
  Put(header, 104, 272); Put(header, 108, 240);
  Put(ids, 144, 240); Put(ids, 148, 320);
  Put(ids, 152, 0); Put(ids, 156, 1);
  Put(ids, 160, 0); Put(ids, 164, 0x11);
  Put(ids, 192, 1); Put(ids, 196, 0x401);
  const char* first = "Lexample/LongDefiningClassNameBeyondSmallStringOptimization;";
  data[240] = static_cast<unsigned char>(std::strlen(first));
  std::strcpy(reinterpret_cast<char*>(data + 241), first);
  data[320] = 9; std::memcpy(data + 321, "LFixture;", 10);
}
static void Verify(const MappedDexMetadata& view) {
  std::string error; MappedDexClassIdentity identity;
  forbid_allocation = true;
  assert(ReadMappedDexClassIdentity(view, &identity, &error));
  assert(identity.access == 0x11 && std::strstr(identity.descriptor, "LongDefining"));
  auto second = view; second.class_def_index = 1;
  assert(ReadMappedDexClassIdentity(second, &identity, &error));
  assert(identity.access == 0x401 && !std::strcmp(identity.descriptor, "LFixture;"));
  forbid_allocation = false;
  const auto saved = identity;
  auto bad = view; bad.class_def_index = 2;
  assert(!ReadMappedDexClassIdentity(bad, &identity, &error));
  assert(identity.descriptor == saved.descriptor && identity.access == saved.access);
  bad = view; bad.size = 111;
  assert(!ReadMappedDexClassIdentity(bad, &identity, &error));
  assert(!ReadMappedDexClassIdentity(view, nullptr, &error));
  assert(!ReadMappedDexClassIdentity(view, &identity, nullptr));
}
int main() {
  Bytes standard{}; Fixture(standard.data(), standard.data(), standard.data(), "dex\n035", 112);
  MappedDexMetadata view{standard.data(), 512, standard.data(), 512, 0}; Verify(view);
  // Flags/identity do not require allocating or parsing the unrelated complete
  // member set. Invalid member data still fails the separate full parser.
  Put(standard.data(), 184, UINT32_MAX);
  MappedDexClassIdentity identity; ClassSchema full; std::string error;
  assert(ReadMappedDexClassIdentity(view, &identity, &error));
  assert(!ReadMappedDexDeclarations(view, &full, &error));
  Put(standard.data(), 184, 0);
  Put(standard.data(), 160, 2);
  identity = {"sentinel", 99}; assert(!ReadMappedDexClassIdentity(view, &identity, &error));
  assert(identity.access == 99 && !std::strcmp(identity.descriptor, "sentinel"));
  Put(standard.data(), 160, 0); standard[241] = '[';
  assert(!ReadMappedDexClassIdentity(view, &identity, &error));
  Bytes compact{}, data{};
  Fixture(compact.data(), compact.data(), data.data(), "cdex001", 136);
  Put(compact.data(), 108, 0);
  Verify({compact.data(), 512, data.data(), 512, 0});
  Bytes container{}; auto* header = container.data() + 512;
  Fixture(header, container.data(), container.data(), "dex\n041", 120);
  Put(header, 104, 0); Put(header, 108, 0);
  Put(header, 112, 1024); Put(header, 116, 512);
  Verify({header, 512, container.data(), container.size(), 0});
  std::cout << "Bounded Standard/Compact/container ClassDef identity: no allocation, exact index/type/flags, unchanged output refusal, independent full-member parsing PASS\n";
}

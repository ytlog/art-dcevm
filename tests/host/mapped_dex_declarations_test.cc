#include "evolution/class_schema.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
using namespace artdcevm::evolution;
namespace {
uint32_t U32(const std::vector<unsigned char>& bytes, size_t off) {
  uint32_t value; std::memcpy(&value, bytes.data() + off, sizeof(value)); return value;
}
void Put(std::vector<unsigned char>& bytes, size_t off, uint32_t value) { std::memcpy(bytes.data() + off, &value, sizeof(value)); }
uint32_t Leb(const std::vector<unsigned char>& bytes, size_t* cursor) {
  uint32_t result = 0;
  for (unsigned n = 0; n < 5; ++n) {
    unsigned byte = bytes.at((*cursor)++); result |= (byte & 0x7f) << (7 * n);
    if (!(byte & 0x80)) return result;
  }
  std::abort();
}
void Checksum(std::vector<unsigned char>& bytes) {
  uint32_t a = 1, b = 0;
  for (size_t i = 12; i < bytes.size(); ++i) { a = (a + bytes[i]) % 65521; b = (b + a) % 65521; }
  Put(bytes, 8, (b << 16) | a);
}
std::vector<unsigned char> File(const char* path) {
  std::ifstream input(path, std::ios::binary); assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Same(const ClassSchema& a, const ClassSchema& b) {
  assert(a.descriptor == b.descriptor && a.superclass == b.superclass && a.interfaces == b.interfaces);
  assert(a.access == b.access && a.fields.size() == b.fields.size() && a.methods.size() == b.methods.size());
  auto members = [](const auto& first, const auto& second) {
    for (size_t i = 0; i < first.size(); ++i)
      assert(first[i].name == second[i].name && first[i].descriptor == second[i].descriptor &&
             first[i].access == second[i].access && first[i].dex_index == second[i].dex_index);
  };
  members(a.fields, b.fields); members(a.methods, b.methods);
}
ClassSchema Parse(const std::vector<unsigned char>& main, const std::vector<unsigned char>& data, uint32_t index = 0) {
  ClassSchema result; std::string error;
  if (!ReadMappedDexDeclarations({main.data(), main.size(), data.data(), data.size(), index}, &result, &error)) {
    std::cerr << error << '\n'; std::abort();
  }
  return result;
}
void Reject(const MappedDexMetadata& view) {
  ClassSchema sentinel; sentinel.descriptor = "Lsentinel;";
  std::string error;
  assert(!ReadMappedDexDeclarations(view, &sentinel, &error));
  assert(!error.empty() && sentinel.descriptor == "Lsentinel;");
}
}
int main(int argc, char** argv) {
  assert(argc == 2);
  auto original = File(argv[1]);
  ClassSchema expected; std::string error;
  assert(ReadDexDeclarations(original, &expected, &error));
  Same(expected, Parse(original, original));
  auto sync = std::find_if(expected.methods.begin(), expected.methods.end(), [](const auto& m) { return m.name == "synchronizedMethod"; });
  assert(sync != expected.methods.end() && (sync->access & 0x20000) && !(sync->access & 0x20));
  // Live DEX checksums and code items may be transformed by ART. Old metadata
  // reads no method bodies; replacement parsing remains strict.
  auto changed = original;
  changed[8] ^= 0xff;
  Same(expected, Parse(changed, changed));
  ClassSchema rejected;
  assert(!ReadDexDeclarations(changed, &rejected, &error));
  auto stale_code = original;
  size_t cursor = U32(original, U32(original, 100) + 24);
  const uint32_t statics = Leb(original, &cursor), instances = Leb(original, &cursor);
  const uint32_t directs = Leb(original, &cursor); Leb(original, &cursor);
  assert(directs != 0);
  for (uint32_t i = 0; i < statics + instances; ++i) { Leb(original, &cursor); Leb(original, &cursor); }
  Leb(original, &cursor); Leb(original, &cursor);
  size_t code_start = cursor; Leb(original, &cursor);
  for (size_t i = code_start; i < cursor; ++i) stale_code[i] = i + 1 == cursor ? 0x7f : 0xff;
  Checksum(stale_code);
  Same(expected, Parse(stale_code, stale_code));
  assert(!ReadDexDeclarations(stale_code, &rejected, &error));
  // ID tables are relative to Begin; all variable metadata is relative to
  // DataBegin. Simulate CompactDex's independent/shared data allocation.
  const size_t data_start = U32(original, 108);
  std::vector<unsigned char> compact(original.begin(), original.begin() + data_start);
  compact.insert(compact.begin() + 0x70, 24, 0);
  std::memcpy(compact.data(), "cdex001\0", 8); Put(compact, 36, 0x88);
  Put(compact, 32, static_cast<uint32_t>(compact.size()));
  for (size_t header : {size_t{56}, size_t{64}, size_t{72}, size_t{80}, size_t{88}, size_t{96}})
    if (U32(compact, header)) Put(compact, header + 4, U32(compact, header + 4) + 24);
  Same(expected, Parse(compact, original));
  // Method references include external owners and do not require class_defs.
  auto no_classes = compact;
  Put(no_classes, 96, 0); Put(no_classes, 100, 0);
  bool external = false;
  for(uint32_t index=0;index<U32(original,88);++index) {
    MappedDexMethodReference standard,shared,without_class;
    assert(ReadMappedDexMethodReference({original.data(),original.size(),original.data(),original.size(),UINT32_MAX},
                                       index,&standard,&error));
    assert(ReadMappedDexMethodReference({compact.data(),compact.size(),original.data(),original.size(),UINT32_MAX},
                                       index,&shared,&error));
    assert(ReadMappedDexMethodReference({no_classes.data(),no_classes.size(),original.data(),original.size(),UINT32_MAX},
                                       index,&without_class,&error));
    assert(standard.proto_id==original.data()+U32(original,76)+size_t{standard.proto_index}*12);
    assert(shared.proto_id==compact.data()+U32(compact,76)+size_t{shared.proto_index}*12);
    assert(standard.class_index==shared.class_index && standard.proto_index==shared.proto_index &&
           standard.declaring_class==shared.declaring_class && standard.name==shared.name &&
           standard.descriptor==shared.descriptor && standard.descriptor==without_class.descriptor);
    external |= standard.declaring_class!=expected.descriptor;
    for(const auto& method:expected.methods)if(method.dex_index==index)
      assert(standard.declaring_class==expected.descriptor && standard.name==method.name &&
             standard.descriptor==method.descriptor);
  }
  assert(external);
  auto reject_reference = [&](const MappedDexMetadata& view,uint32_t index) {
    MappedDexMethodReference sentinel;sentinel.name="sentinel";
    assert(!ReadMappedDexMethodReference(view,index,&sentinel,&error));
    assert(!error.empty() && sentinel.name=="sentinel");
  };
  reject_reference({compact.data(),compact.size(),original.data(),original.size(),0},U32(compact,88));
  reject_reference({compact.data(),compact.size(),original.data(),1,0},0);
  auto bad_reference=compact;
  const auto method_offset=U32(compact,92);
  bad_reference[method_offset]=0xff;bad_reference[method_offset+1]=0xff;
  reject_reference({bad_reference.data(),bad_reference.size(),original.data(),original.size(),0},0);
  bad_reference=compact;
  bad_reference[method_offset+2]=0xff;bad_reference[method_offset+3]=0xff;
  reject_reference({bad_reference.data(),bad_reference.size(),original.data(),original.size(),0},0);
  // Array owners are legal references (e.g. clone); primitive owners are not.
  auto owner_mapping=compact,owner_data=original;
  const uint16_t owner_index=uint16_t(compact[method_offset]) | uint16_t{compact[method_offset+1]}<<8;
  const uint32_t owner_string=U32(compact,U32(compact,68)+size_t{owner_index}*4);
  const std::string array_owner="[Ljava/lang/Object;";
  Put(owner_mapping,U32(compact,60)+size_t{owner_string}*4,static_cast<uint32_t>(owner_data.size()));
  owner_data.push_back(static_cast<unsigned char>(array_owner.size()));
  owner_data.insert(owner_data.end(),array_owner.begin(),array_owner.end());owner_data.push_back(0);
  MappedDexMethodReference array_reference;
  assert(ReadMappedDexMethodReference({owner_mapping.data(),owner_mapping.size(),owner_data.data(),owner_data.size(),0},
                                     0,&array_reference,&error));
  assert(array_reference.declaring_class==array_owner);
  Put(owner_mapping,U32(compact,60)+size_t{owner_string}*4,static_cast<uint32_t>(owner_data.size()));
  owner_data.push_back(1);owner_data.push_back('I');owner_data.push_back(0);
  reject_reference({owner_mapping.data(),owner_mapping.size(),owner_data.data(),owner_data.size(),0},0);
  // Default methods depend on the StandardDex version or CompactDex feature bit.
  for(const char* version:{"035","037","038","039","040"}) {
    auto dex=original;std::memcpy(dex.data()+4,version,3);bool supported=false;
    assert(ReadMappedDexSupportsDefaultMethods({dex.data(),dex.size(),dex.data(),dex.size(),0},&supported,&error));
    assert(supported==(std::strcmp(version,"037")>=0));
  }
  for(uint32_t flags:{0u,1u,2u,3u}) {
    auto dex=compact;Put(dex,0x70,flags);bool supported=false;
    assert(ReadMappedDexSupportsDefaultMethods({dex.data(),dex.size(),original.data(),original.size(),0},&supported,&error));
    assert(supported==((flags&1)!=0));
  }
  bool unchanged=true;
  assert(!ReadMappedDexSupportsDefaultMethods({compact.data(),0x70,original.data(),original.size(),0},&unchanged,&error));
  assert(unchanged && !error.empty());
  // A second compact mapping can borrow exactly the same shared data region.
  auto compact2 = compact;
  Same(expected, Parse(compact2, original));
  // Select a target class in a multi-class ID mapping, without a single-class
  // restriction. The unused second class is a legal Object declaration.
  auto multiple = original;
  const uint32_t original_def = U32(original, 100);
  const uint32_t extra_offset = static_cast<uint32_t>((multiple.size() + 3) & ~size_t{3});
  multiple.resize(extra_offset + 64);
  std::memcpy(multiple.data() + extra_offset, original.data() + original_def, 32);
  std::memcpy(multiple.data() + extra_offset + 32, original.data() + original_def, 32);
  uint32_t object_index = U32(original, original_def + 8);
  Put(multiple, extra_offset + 32, object_index); Put(multiple, extra_offset + 40, UINT32_MAX);
  Put(multiple, extra_offset + 44, 0); Put(multiple, extra_offset + 56, 0);
  Put(multiple, 96, 2); Put(multiple, 100, extra_offset);
  Same(expected, Parse(multiple, original));
  auto object = Parse(multiple, original, 1);
  assert(object.descriptor == "Ljava/lang/Object;" && object.methods.empty() && object.fields.empty());
  Reject({multiple.data(), multiple.size(), original.data(), original.size(), 2});
  for (size_t size = 0; size < 0x88; ++size)
    Reject({compact.data(), size, original.data(), original.size(), 0});
  Reject({compact.data(), compact.size(), original.data(), 1, 0});
  auto bad = compact;
  Put(bad, 88, UINT32_MAX); Reject({bad.data(), bad.size(), original.data(), original.size(), 0});
  Put(bad, 88, U32(compact, 88)); Put(bad, 92, UINT32_MAX);
  Reject({bad.data(), bad.size(), original.data(), original.size(), 0});
  // Mutations hit separate metadata/data bounds, strings and ULEB decoding.
  std::mt19937 random(0xdce013);
  for (unsigned i = 0; i < 10000; ++i) {
    auto main = compact, data = original;
    for (unsigned n = 0; n < 1 + random() % 8; ++n) {
      auto& target = random() & 1 ? main : data;
      target[random() % target.size()] ^= 1 + random() % 255;
    }
    MappedDexMethodReference reference;reference.name="sentinel";
    if(!ReadMappedDexMethodReference({main.data(),main.size(),data.data(),data.size(),UINT32_MAX},
                                    random()%U32(compact,88),&reference,&error))
      assert(reference.name=="sentinel" && !error.empty());
    ClassSchema value; value.descriptor = "Lsentinel;";
    if (!ReadMappedDexDeclarations({main.data(), main.size(), data.data(), data.size(), 0}, &value, &error))
      assert(value.descriptor == "Lsentinel;" && !error.empty());
  }
  std::cout << "mapped DEX declarations: standard, changed checksum, CompactDex/shared data, selected multiclass, external/array method references without class_defs, default-method version/features, bounds and 10000 mutations passed\n";
}

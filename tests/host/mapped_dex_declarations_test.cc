#include "evolution/class_schema.h"
#include "platform/common/dex_mapping_abi.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
using namespace artdcevm::evolution;
namespace dex_mapping_abi = artdcevm::dex_mapping_abi;
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
void CheckFieldReferences(const MappedDexMetadata& view) {
  ClassSchema schema;std::string error;
  auto declarations=view;declarations.class_def_index=0;
  assert(ReadMappedDexDeclarations(declarations,&schema,&error));
  for(const auto& field:schema.fields) {
    MappedDexFieldReference reference;
    assert(ReadMappedDexFieldReference(view,field.dex_index,&reference,&error));
    assert(std::string(reference.name)==field.name&&std::string(reference.descriptor)==field.descriptor);
    assert(std::string(reference.declaring_class)==schema.descriptor);
  }
  uint32_t count;std::memcpy(&count,view.begin+80,4);
  MappedDexFieldReference sentinel{"owner","name","type"};
  assert(!ReadMappedDexFieldReference(view,count,&sentinel,&error));
  assert(std::string(sentinel.name)=="name"&&!error.empty());
  assert(!ReadMappedDexFieldReference(view,0,nullptr,&error));
  assert(!ReadMappedDexFieldReference(view,0,&sentinel,nullptr));
}
void CheckMethodMatches(const MappedDexMetadata& view) {
  uint32_t count;std::memcpy(&count,view.begin+88,4);
  std::string error;
  for(uint32_t i=0;i<count;++i) {
    MappedDexMethodReference reference;
    assert(ReadMappedDexMethodReference(view,i,&reference,&error));
    bool matched = false;
    assert(ReadMappedDexMethodMatches(view,i,reference.name,reference.descriptor,&matched,&error) && matched && error.empty());
    for(const auto& wrong:{std::string(),reference.descriptor+"I",std::string("I")}) {
      assert(ReadMappedDexMethodMatches(view,i,reference.name,wrong,&matched,&error) && !matched);
    }
    assert(ReadMappedDexMethodMatches(view,i,reference.name+"!",reference.descriptor,&matched,&error) && !matched);
  }
  bool sentinel=true;
  assert(!ReadMappedDexMethodMatches(view,count,"x","()V",&sentinel,&error) && sentinel && !error.empty());
  assert(!ReadMappedDexMethodMatches(view,0,"x","()V",nullptr,&error));
  assert(!ReadMappedDexMethodMatches(view,0,"x","()V",&sentinel,nullptr));
}
void CheckTypes(const MappedDexMetadata& view) {
  auto u32 = [](const unsigned char* p) { uint32_t v; std::memcpy(&v,p,4); return v; };
  const auto* ids = std::memcmp(view.begin+4,"041",3)==0 ? view.data_begin : view.begin;
  const uint32_t count = u32(view.begin+64);
  std::string error;
  for(uint32_t i=0;i<count;++i) {
    const uint32_t string = u32(ids+u32(view.begin+68)+size_t{i}*4);
    size_t at = u32(ids+u32(view.begin+60)+size_t{string}*4);
    while(view.data_begin[at++]&0x80) {}
    const char* value = nullptr;
    auto no_class = view; no_class.class_def_index = UINT32_MAX;
    assert(ReadMappedDexTypeDescriptor(no_class,i,&value,&error) && error.empty());
    assert(value==reinterpret_cast<const char*>(view.data_begin+at));
    assert(IsTypeDescriptor(value,true));
  }
  const char* sentinel = "unchanged";
  const auto* saved = sentinel;
  for(uint32_t bad:{count,UINT32_MAX}) {
    assert(!ReadMappedDexTypeDescriptor(view,bad,&sentinel,&error));
    assert(sentinel==saved && !error.empty());
  }
  assert(!ReadMappedDexTypeDescriptor(view,0,nullptr,&error));
  assert(!ReadMappedDexTypeDescriptor(view,0,&sentinel,nullptr));
}
void CheckParent(const MappedDexMetadata& view,const std::string& expected) {
  std::string error="old";
  const char* value="old";
  assert(ReadMappedDexSuperclassDescriptor(view,&value,&error) && error.empty());
  assert(expected.empty()?value==nullptr:value && expected==value);
  assert(!ReadMappedDexSuperclassDescriptor(view,nullptr,&error));
  assert(!ReadMappedDexSuperclassDescriptor(view,&value,nullptr));
  auto invalid=view;invalid.class_def_index=UINT32_MAX;
  value="unchanged";const char* saved=value;
  assert(!ReadMappedDexSuperclassDescriptor(invalid,&value,&error));
  assert(value==saved && !error.empty());
}
void CheckHierarchy(const MappedDexMetadata& view,const ClassSchema& expected) {
  std::string error;
  MappedDexHierarchy hierarchy;
  assert(ReadMappedDexHierarchy(view,&hierarchy,&error));
  assert(hierarchy.superclass==expected.superclass && hierarchy.interfaces==expected.interfaces && error.empty());
  MappedDexHierarchy sentinel{"Lsentinel;",{"LIface;"}};
  auto invalid=view;invalid.class_def_index=UINT32_MAX;
  assert(!ReadMappedDexHierarchy(invalid,&sentinel,&error));
  assert(sentinel.superclass=="Lsentinel;" && sentinel.interfaces==std::vector<std::string>{"LIface;"});
  assert(!ReadMappedDexHierarchy(view,nullptr,&error));
  assert(!ReadMappedDexHierarchy(view,&sentinel,nullptr));
}
ClassSchema Parse(const std::vector<unsigned char>& main, const std::vector<unsigned char>& data, uint32_t index = 0) {
  ClassSchema result; std::string error;
  if (!ReadMappedDexDeclarations({main.data(), main.size(), data.data(), data.size(), index}, &result, &error)) {
    std::cerr << error << '\n'; std::abort();
  }
  CheckTypes({main.data(),main.size(),data.data(),data.size(),index});
  CheckMethodMatches({main.data(),main.size(),data.data(),data.size(),UINT32_MAX});
  CheckFieldReferences({main.data(),main.size(),data.data(),data.size(),UINT32_MAX});
  CheckParent({main.data(),main.size(),data.data(),data.size(),index},result.superclass);
  CheckHierarchy({main.data(),main.size(),data.data(),data.size(),index},result);
  MappedDexMemberCounts counts;
  assert(ReadMappedDexMemberCounts({main.data(),main.size(),data.data(),data.size(),index},&counts,&error));
  uint32_t statics=0,directs=0;
  for(const auto& field:result.fields)statics+=field.IsStatic();
  for(const auto& method:result.methods)
    directs+=((method.access&0x000a)!=0 || method.name=="<init>" || method.name=="<clinit>");
  assert(counts.static_fields==statics && counts.instance_fields==result.fields.size()-statics &&
      counts.direct_methods==directs && counts.virtual_methods==result.methods.size()-directs);

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
  // superclass_idx is a full 32-bit value; the NO_INDEX root is distinct
  // from an invalid index that happens to truncate to an existing type.
  auto parent_variant=original;
  const size_t superclass=U32(original,100)+8;
  const uint32_t parent_index=U32(original,superclass);
  Put(parent_variant,superclass,UINT32_MAX);
  CheckParent({parent_variant.data(),parent_variant.size(),parent_variant.data(),parent_variant.size(),0},"");
  Put(parent_variant,superclass,parent_index+0x10000);
  const char* parent_sentinel="unchanged";const auto* saved_parent=parent_sentinel;
  assert(!ReadMappedDexSuperclassDescriptor({parent_variant.data(),parent_variant.size(),parent_variant.data(),parent_variant.size(),0},&parent_sentinel,&error));
  assert(parent_sentinel==saved_parent && !error.empty());
  MappedDexHierarchy high_sentinel{"Lsentinel;",{"LIface;"}};
  assert(!ReadMappedDexHierarchy({parent_variant.data(),parent_variant.size(),parent_variant.data(),parent_variant.size(),0},&high_sentinel,&error));
  assert(high_sentinel.superclass=="Lsentinel;" && high_sentinel.interfaces==std::vector<std::string>{"LIface;"});

  // The source size slot changes semantics at API 35. Its zero/nonzero junk
  // must never determine a modern file extent; legacy APIs keep their extent.
  auto normalize = [&](int api, const dex_mapping_abi::Prefix& prefix) {
    MappedDexMetadata view{};
    assert(dex_mapping_abi::Normalize(api, prefix, 0, &view, &error));
    ClassSchema value;
    assert(ReadMappedDexDeclarations(view, &value, &error));
    Same(expected, value);
    return view;
  };
  for (int api = 30; api <= 37; ++api) {
    dex_mapping_abi::Prefix prefix{nullptr, original.data(), original.size(), original.data(), original.size()};
    if (api >= 35) prefix.size = 0;
    assert(normalize(api, prefix).size == original.size());
    if (api >= 35) { prefix.size = SIZE_MAX; assert(normalize(api, prefix).size == original.size()); }
  }
  auto reject_mapping = [&](int api, const dex_mapping_abi::Prefix& prefix) {
    MappedDexMetadata unchanged{nullptr, 123, nullptr, 456, 789};
    assert(!dex_mapping_abi::Normalize(api, prefix, 0, &unchanged, &error));
    assert(unchanged.size == 123 && unchanged.data_size == 456 && unchanged.class_def_index == 789 && !error.empty());
  };
  dex_mapping_abi::Prefix prefix{nullptr, original.data(), original.size(), original.data(), original.size()};
  reject_mapping(29, prefix); reject_mapping(38, prefix);
  prefix.size = 0; reject_mapping(33, prefix);
  prefix.data_size = 0x6f; reject_mapping(35, prefix);
  prefix.data_size = original.size() - 1; reject_mapping(35, prefix);
  prefix.data_size = SIZE_MAX; reject_mapping(35, prefix);

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
  for (int api = 30; api <= 34; ++api)
    normalize(api, {nullptr, compact.data(), compact.size(), original.data(), original.size()});
  for (int api = 35; api <= 37; ++api)
    reject_mapping(api, {nullptr, compact.data(), 0, original.data(), original.size()});

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
  // v041 offsets are container-relative, including ID tables and ProtoId.
  // Select a later header while borrowing metadata from the earlier region.
  auto container = original;
  const size_t header_offset = (container.size() + 3) & ~size_t{3};
  container.resize(header_offset);
  container.insert(container.end(), original.begin(), original.begin() + data_start);
  container.insert(container.begin() + header_offset + 0x70, 8, 0);
  auto header_put = [&](size_t off, uint32_t value) { Put(container, header_offset + off, value); };
  std::memcpy(container.data() + header_offset + 4, "041", 3);
  header_put(36, 0x78); header_put(32, static_cast<uint32_t>(container.size() - header_offset));
  header_put(104, 0); header_put(108, 0);
  header_put(0x70, static_cast<uint32_t>(container.size()));
  header_put(0x74, static_cast<uint32_t>(header_offset));
  for (size_t off : {size_t{56}, size_t{64}, size_t{72}, size_t{80}, size_t{88}, size_t{96}})
    if (U32(original, off)) header_put(off + 4, static_cast<uint32_t>(header_offset + U32(original, off + 4) + 8));
  auto container_view = [&]() { return MappedDexMetadata{container.data() + header_offset,
      container.size() - header_offset, container.data(), container.size(), 0}; };
  ClassSchema container_schema;
  assert(ReadMappedDexDeclarations(container_view(), &container_schema, &error));
  Same(expected, container_schema);
  CheckTypes(container_view());
  CheckMethodMatches(container_view());
  CheckFieldReferences(container_view());
  CheckParent(container_view(),expected.superclass);
  CheckHierarchy(container_view(),expected);
  MappedDexMemberCounts container_counts, original_counts;
  assert(ReadMappedDexMemberCounts(container_view(),&container_counts,&error));
  assert(ReadMappedDexMemberCounts({original.data(),original.size(),original.data(),original.size(),0},&original_counts,&error));
  assert(container_counts.static_fields==original_counts.static_fields &&
      container_counts.instance_fields==original_counts.instance_fields &&
      container_counts.direct_methods==original_counts.direct_methods &&
      container_counts.virtual_methods==original_counts.virtual_methods);
  for (uint32_t index = 0; index < U32(original, 88); ++index) {
    MappedDexMethodReference old_ref, new_ref;
    assert(ReadMappedDexMethodReference({original.data(), original.size(), original.data(), original.size(), 0}, index, &old_ref, &error));
    assert(ReadMappedDexMethodReference(container_view(), index, &new_ref, &error));
    assert(old_ref.name == new_ref.name && old_ref.descriptor == new_ref.descriptor &&
           old_ref.declaring_class == new_ref.declaring_class);
    assert(new_ref.proto_id == container.data() + U32(container, header_offset + 76) + size_t{new_ref.proto_index} * 12);
  }
  bool container_defaults = false;
  assert(ReadMappedDexSupportsDefaultMethods(container_view(), &container_defaults, &error) && container_defaults);
  // A first header (offset zero) may share the later header's ID tables.
  auto first_container = container;
  std::memcpy(first_container.data() + 4, "041", 3);
  Put(first_container, 36, 0x78); Put(first_container, 32, static_cast<uint32_t>(first_container.size()));
  Put(first_container, 104, 0); Put(first_container, 108, 0);
  Put(first_container, 0x70, static_cast<uint32_t>(first_container.size())); Put(first_container, 0x74, 0);
  for (size_t off : {size_t{56}, size_t{64}, size_t{72}, size_t{80}, size_t{88}, size_t{96}})
    Put(first_container, off + 4, U32(container, header_offset + off + 4));
  Same(expected, Parse(first_container, first_container));

  for (int api = 35; api <= 37; ++api) {
    const auto view = container_view();
    assert(normalize(api, {nullptr, view.begin, 0, view.data_begin, view.data_size}).size == view.size);
    normalize(api, {nullptr, first_container.data(), SIZE_MAX, first_container.data(), first_container.size()});
  }
  const auto valid_container = container;
  for (auto mutation : {std::pair<size_t, uint32_t>{0x70, UINT32_MAX}, {0x74, 0},
                        {32, 0x70}, {32, UINT32_MAX}, {36, 0x70}, {104, 1}, {108, 4}, {92, UINT32_MAX}}) {
    container = valid_container; header_put(mutation.first, mutation.second);
    Reject(container_view());
    reject_reference(container_view(), 0);
  }
  container = valid_container;
  auto short_container = container_view(); --short_container.data_size;
  Reject(short_container);
  auto short_header = container_view(); short_header.size = 0x77;
  Reject(short_header);
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
  // Exercise container-relative bounds with deterministic mutations too.
  std::mt19937 container_random(0xdce041);
  for (unsigned i = 0; i < 10000; ++i) {
    container = valid_container;
    for (unsigned n = 0; n < 1 + container_random() % 8; ++n)
      container[container_random() % container.size()] ^= 1 + container_random() % 255;
    bool matched=true;
    const auto& method=expected.methods.front();
    if(!ReadMappedDexMethodMatches(container_view(),method.dex_index,method.name,method.descriptor,&matched,&error))
      assert(matched && !error.empty());
    MappedDexMemberCounts counts{11,22,33,44};
    MappedDexHierarchy hierarchy_sentinel{"Lsentinel;",{"LIface;"}};
    if(!ReadMappedDexHierarchy(container_view(),&hierarchy_sentinel,&error))
      assert(hierarchy_sentinel.superclass=="Lsentinel;" && hierarchy_sentinel.interfaces==std::vector<std::string>{"LIface;"});
    if(!ReadMappedDexMemberCounts(container_view(),&counts,&error))
      assert(counts.static_fields==11 && counts.instance_fields==22 && counts.direct_methods==33 && counts.virtual_methods==44 && !error.empty());
    ClassSchema value; value.descriptor = "Lsentinel;";
    if (!ReadMappedDexDeclarations(container_view(), &value, &error))
      assert(value.descriptor == "Lsentinel;" && !error.empty());
    MappedDexMethodReference reference; reference.name = "sentinel";
    if (!ReadMappedDexMethodReference(container_view(), container_random() % U32(original, 88), &reference, &error))
      assert(reference.name == "sentinel" && !error.empty());
  }
  MappedDexMemberCounts sentinel{11,22,33,44};
  auto counts_bad=compact;
  Put(counts_bad,U32(compact,100)+24,UINT32_MAX);
  assert(!ReadMappedDexMemberCounts({counts_bad.data(),counts_bad.size(),original.data(),original.size(),0},&sentinel,&error));
  assert(sentinel.static_fields==11 && sentinel.instance_fields==22 && sentinel.direct_methods==33 && sentinel.virtual_methods==44);
  assert(!ReadMappedDexMemberCounts({compact.data(),compact.size(),original.data(),original.size(),UINT32_MAX},&sentinel,&error));
  // The count-only reader must bound all four ULEBs without needing member bodies.
  for(const std::vector<unsigned char>& encoded : std::vector<std::vector<unsigned char>>{
      {1}, {0,1}, {0,0,1}, {0x80,0x80,0x80,0x80,0x10},
      {0xff,0xff,0xff,0xff,0x0f,0,0,0},
      {0,0,0xff,0xff,0xff,0xff,0x0f,0}}) {
    auto header=compact, truncated=original;
    Put(header,U32(header,100)+24,static_cast<uint32_t>(truncated.size()));
    truncated.insert(truncated.end(),encoded.begin(),encoded.end());
    assert(!ReadMappedDexMemberCounts({header.data(),header.size(),truncated.data(),truncated.size(),0},&sentinel,&error));
    assert(sentinel.static_fields==11 && sentinel.instance_fields==22 &&
        sentinel.direct_methods==33 && sentinel.virtual_methods==44 && !error.empty());
  }
  // Mutations hit separate metadata/data bounds, strings and ULEB decoding.
  std::mt19937 random(0xdce013);
  for (unsigned i = 0; i < 10000; ++i) {
    auto main = compact, data = original;
    for (unsigned n = 0; n < 1 + random() % 8; ++n) {
      auto& target = random() & 1 ? main : data;
      target[random() % target.size()] ^= 1 + random() % 255;
    }
    bool matched=true;
    const auto& method=expected.methods.front();
    if(!ReadMappedDexMethodMatches({main.data(),main.size(),data.data(),data.size(),UINT32_MAX},
                                  method.dex_index,method.name,method.descriptor,&matched,&error))
      assert(matched && !error.empty());
    MappedDexMethodReference reference;reference.name="sentinel";
    if(!ReadMappedDexMethodReference({main.data(),main.size(),data.data(),data.size(),UINT32_MAX},
                                    random()%U32(compact,88),&reference,&error))
      assert(reference.name=="sentinel" && !error.empty());
    MappedDexMemberCounts counts{11,22,33,44};
    MappedDexHierarchy hierarchy_sentinel{"Lsentinel;",{"LIface;"}};
    if(!ReadMappedDexHierarchy({main.data(),main.size(),data.data(),data.size(),0},&hierarchy_sentinel,&error))
      assert(hierarchy_sentinel.superclass=="Lsentinel;" && hierarchy_sentinel.interfaces==std::vector<std::string>{"LIface;"});
    if(!ReadMappedDexMemberCounts({main.data(),main.size(),data.data(),data.size(),0},&counts,&error))
      assert(counts.static_fields==11 && counts.instance_fields==22 && counts.direct_methods==33 && counts.virtual_methods==44 && !error.empty());
    ClassSchema value; value.descriptor = "Lsentinel;";
    if (!ReadMappedDexDeclarations({main.data(), main.size(), data.data(), data.size(), 0}, &value, &error))
      assert(value.descriptor == "Lsentinel;" && !error.empty());
  }
  std::cout << "mapped DEX declarations: standard, changed checksum, CompactDex/shared data, API30-37 mapping extents, DEX041 container first/later headers and shared IDs, selected multiclass member counts, external/array method references without class_defs, default-method version/features, bounds and 20000 CompactDex/container mutations passed\n";
}

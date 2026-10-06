#include "evolution/class_schema.h"
#include "evolution/migration_plan.h"
#include "support/json.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

using namespace artdcevm::evolution;
namespace {
int assertions = 0;
void Check(bool condition, const std::string& message) {
  ++assertions;
  if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> Read(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  Check(input.good(), "Missing fixture: " + path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
ClassSchema Schema(const std::vector<unsigned char>& bytes) {
  ClassSchema result; std::string error;
  Check(ReadDexSchema(bytes, &result, &error), "ReadDexSchema: " + error);
  return result;
}
MigrationPlan Plan(const ClassSchema& before, const ClassSchema& after) {
  MigrationPlan result; std::string error;
  Check(PlanMigration(before, after, &result, &error), "PlanMigration: " + error);
  return result;
}
void MappingInvariants(const MigrationPlan& plan) {
  auto check = [](const auto& before, const auto& after, const auto& mappings) {
    std::vector<int> old_seen(before.size()), new_seen(after.size());
    for (const auto& m : mappings) {
      if (m.old_index != kAbsent) { Check(m.old_index < before.size(), "Invalid old index"); ++old_seen[m.old_index]; }
      if (m.new_index != kAbsent) { Check(m.new_index < after.size(), "Invalid new index"); ++new_seen[m.new_index]; }
      if (m.action == MemberAction::kRetain) {
        Check(m.old_index != kAbsent && m.new_index != kAbsent, "Retain requires two members");
        const auto& a = before[m.old_index]; const auto& b = after[m.new_index];
        Check(a.name == b.name && a.descriptor == b.descriptor && a.IsStatic() == b.IsStatic(), "Unsafe member copy");
      } else if (m.action == MemberAction::kInitialize) {
        Check(m.old_index == kAbsent && m.new_index != kAbsent, "Initialize must not read old storage");
      } else Check(m.old_index != kAbsent && m.new_index == kAbsent, "Retire must not write new storage");
    }
    Check(std::all_of(old_seen.begin(), old_seen.end(), [](int n) { return n == 1; }), "Old member missing/duplicated");
    Check(std::all_of(new_seen.begin(), new_seen.end(), [](int n) { return n == 1; }), "New member missing/duplicated");
  };
  check(plan.before.fields, plan.after.fields, plan.fields);
  check(plan.before.methods, plan.after.methods, plan.methods);
}
uint32_t U32(const std::vector<unsigned char>& b, size_t at) {
  return b.at(at) | (uint32_t{b.at(at+1)} << 8) | (uint32_t{b.at(at+2)} << 16) | (uint32_t{b.at(at+3)} << 24);
}
void Put32(std::vector<unsigned char>* b, size_t at, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) b->at(at+i) = (value >> (8*i)) & 255;
}
void Checksum(std::vector<unsigned char>* bytes) {
  if (bytes->size() < 12) return;
  uint32_t a = 1, b = 0;
  for (size_t i = 12; i < bytes->size(); ++i) { a = (a + bytes->at(i)) % 65521; b = (b+a) % 65521; }
  Put32(bytes, 8, (b << 16) | a);
}
void MustReject(const std::vector<unsigned char>& bytes, const char* description) {
  ClassSchema output; output.descriptor = "Lsentinel;"; std::string error;
  Check(!ReadDexSchema(bytes, &output, &error), description);
  Check(output.descriptor == "Lsentinel;" && !error.empty(), "Parse error must leave output unchanged");
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Pass generated fixture asset directory");
    std::string root = argv[1], suffix = "/dev/artdcevm/fixture/Evolving.dex";
    auto body_bytes = Read(root + "/body" + suffix);
    auto add_bytes = Read(root + "/add" + suffix);
    auto body = Schema(body_bytes), add = Schema(add_bytes);
    auto add2 = Schema(Read(root + "/add2" + suffix));
    auto deleted = Schema(Read(root + "/delete" + suffix));
    auto types = Schema(Read(root + "/types" + suffix));
    auto hierarchy = Schema(Read(root + "/hierarchy" + suffix));
    auto metadata = Schema(Read(root + "/metadata/dev/artdcevm/fixture/SchemaSubject.dex"));
    Check(body.descriptor == "Ldev/artdcevm/fixture/Evolving;" && body.fields.size() == 2, "Wrong class/fields");
    auto additive = Plan(body, add);
    Check(additive.required_features == (kAddField | kAddMethod), "Wrong additive requirements");
    auto same_schema = Plan(add, add2);
    Check(same_schema.required_features == 0 && !same_schema.ChangesLayout(), "Body change must preserve schema");
    auto deletion = Plan(add, deleted);
    Check(deletion.required_features == (kRemoveField | kRemoveMethod), "Deleted members not recognized");
    auto change_types = Plan(add, types);
    Check((change_types.required_features & (kChangeFieldType | kChangeMethodSignature)) ==
          (kChangeFieldType | kChangeMethodSignature), "Type/signature evolution not recognized");
    auto change_hierarchy = Plan(body, hierarchy);
    Check((change_hierarchy.required_features & (kChangeSuperclass | kChangeInterfaces)) ==
          (kChangeSuperclass | kChangeInterfaces), "Hierarchy evolution not recognized");
    for (const auto* plan : {&additive, &same_schema, &deletion, &change_types, &change_hierarchy}) MappingInvariants(*plan);
    Check(metadata.fields.size() == 11, "Primitive/reference/array/Unicode fields");
    auto sync = std::find_if(metadata.methods.begin(), metadata.methods.end(), [](const Member& m) { return m.name == "synchronizedMethod"; });
    Check(sync != metadata.methods.end() && (sync->access & 0x20), "Synchronized flag normalization");
    auto ctor = std::find_if(metadata.methods.begin(), metadata.methods.end(), [](const Member& m) { return m.name == "<init>"; });
    Check(ctor != metadata.methods.end() && ctor->access == 1, "Constructor ART flag must not appear as a Java modifier");
    auto storage = body; storage.fields[0].access ^= kStatic;
    auto storage_plan = Plan(body, storage); MappingInvariants(storage_plan);
    Check(storage_plan.required_features & kChangeStorage, "Static/instance change must allocate new storage");
    auto renamed = body; renamed.fields[0].name = "renamed";
    auto rename_plan = Plan(body, renamed); MappingInvariants(rename_plan);
    Check(rename_plan.required_features == (kAddField | kRemoveField), "Renamed field must not inherit unrelated old value");
    auto reordered = metadata;
    std::reverse(reordered.fields.begin(), reordered.fields.end()); std::reverse(reordered.methods.begin(), reordered.methods.end());
    auto reordered_plan = Plan(metadata, reordered); MappingInvariants(reordered_plan);
    Check(reordered_plan.required_features == 0, "DEX index/order must not define member identity");
    auto duplicates = body; duplicates.fields.push_back(duplicates.fields.front());
    MigrationPlan unchanged; unchanged.required_features = 0xffffffff; std::string error;
    Check(!PlanMigration(body, duplicates, &unchanged, &error) && unchanged.required_features == 0xffffffff, "Invalid plan must be atomic");
    auto wrong_name = body; wrong_name.descriptor = "Lother/Class;";
    Check(!PlanMigration(body, wrong_name, &unchanged, &error), "Cannot redefine another class identity");
    Check(IsMethodDescriptor("([[ILjava/lang/String;)V") && !IsMethodDescriptor("(V)V") &&
          !IsTypeDescriptor(std::string(1, '\0'), true) && !IsTypeDescriptor("[V", true), "Descriptor edge cases");
    Check(artdcevm::QuoteJsonMutf8("\xed\xa0\xbd\xed\xb8\x80") == "\"\\ud83d\\ude00\"", "Modified UTF-8 surrogate encoding");
    for (size_t length = 0; length < body_bytes.size(); ++length)
      MustReject({body_bytes.begin(), body_bytes.begin() + length}, "Truncated DEX accepted");
    auto corrupt = body_bytes; corrupt.back() ^= 1; MustReject(corrupt, "Checksum corruption accepted");
    corrupt = body_bytes; Put32(&corrupt, 60, UINT32_MAX); Checksum(&corrupt); MustReject(corrupt, "Out-of-range table offset accepted");
    corrupt = body_bytes; Put32(&corrupt, 96, 2); Checksum(&corrupt); MustReject(corrupt, "Multiclass patch accepted");
    corrupt = body_bytes;
    size_t string_item = U32(corrupt, 60), string_data = U32(corrupt, string_item);
    for (size_t i = 0; i < 5; ++i) corrupt.at(string_data + i) = 0xff;
    Checksum(&corrupt); MustReject(corrupt, "Overflowing ULEB128 accepted");
    // Recompute checksum so malformed offsets/counts reach the parser's inner bounds checks.
    std::mt19937 random(0xdce013);
    for (unsigned i = 0; i < 10000; ++i) {
      auto fuzz = (i & 1) ? body_bytes : add_bytes;
      for (unsigned j = 0, n = 1 + random() % 8; j < n; ++j) fuzz[random() % fuzz.size()] ^= 1 + random() % 255;
      Checksum(&fuzz);
      ClassSchema parsed; std::string why;
      if (ReadDexSchema(fuzz, &parsed, &why)) {
        MigrationPlan self;
        Check(PlanMigration(parsed, parsed, &self, &why), "Accepted schema must be internally consistent");
        MappingInvariants(self);
      }
    }
    std::cout << "{\"assertions\":" << assertions << ",\"mutatedDexInputs\":10000,\"passed\":true,\"plans\":["
              << DescribeMigration(deletion) << ',' << DescribeMigration(change_types) << ',' << DescribeMigration(change_hierarchy) << "]}\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL: " << failure.what() << '\n'; return 1;
  }
}

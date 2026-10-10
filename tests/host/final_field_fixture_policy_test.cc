#include "platform/common/native_method_evolution_policy.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace artdcevm;
evolution::ClassSchema Read(const char* file) {
  std::ifstream input(file, std::ios::binary);
  assert(input);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
  evolution::ClassSchema schema; std::string error;
  assert(evolution::ReadDexDeclarations(bytes, &schema, &error));
  return schema;
}
int main(int argc, char** argv) {
  assert(argc == 4);
  auto before = Read(argv[1]), after = Read(argv[2]), folded = Read(argv[3]);
  std::string error; std::vector<native_schema_policy::Requirement> requirements;
  // Public JVMTI exposes Java flags; DEX also carries the constructor bit.
  // The policy must retain equal name/full-descriptor/staticness identities.
  for (auto& method : before.methods) method.access &= 0xffffu;
  assert(before.fields.size() == 2 && after.fields.size() == 2);
  for (const auto& field : before.fields) {
    auto next = std::find_if(after.fields.begin(), after.fields.end(), [&](const auto& f) {
      return f.name == field.name && f.descriptor == field.descriptor;
    });
    assert(next != after.fields.end() && (field.access ^ next->access) == 0x10u);
  }
  assert(before.methods.size() == after.methods.size());
  assert(native_schema_policy::PrepareMethodEvolution(before, after, &requirements, &error));
  assert(requirements.empty());
  requirements = {{native_schema_policy::GuardKind::LoadedSubclasses, "sentinel", "()V"}};
  // The previous constant-field fixture really erased its class initializer.
  assert(!native_schema_policy::PrepareMethodEvolution(before, folded, &requirements, &error));
  assert(error == "Constructor or class initializer retirement lacks dispatch proof");
  assert(requirements.size() == 1 && requirements[0].member_name == "sentinel");
  for (const char* name : {"<init>", "<clinit>"}) {
    auto missing = after;
    missing.methods.erase(std::remove_if(missing.methods.begin(), missing.methods.end(),
        [&](const auto& m) { return m.name == name; }), missing.methods.end());
    assert(!native_schema_policy::PrepareMethodEvolution(before, missing, &requirements, &error));
    assert(requirements.size() == 1 && requirements[0].member_name == "sentinel");
  }
  std::cout << "Real final-field DEX: Java/DEX flags retain initializers; genuine retirement rejected\n";
}

#include "evolution/class_schema.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>

namespace {
// Only allocations inside ReadDexSchema are armed. Fail exactly once, then
// disable injection so exception propagation and test reporting can allocate.
bool allocation_armed = false;
size_t allocations = 0;
size_t failure_at = std::numeric_limits<size_t>::max();
bool failure_triggered = false;
void* Allocate(size_t size) {
  if (allocation_armed && allocations++ == failure_at) {
    failure_triggered = true;
    allocation_armed = false;
    throw std::bad_alloc();
  }
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void Arm(size_t index) {
  allocations = 0;
  failure_at = index;
  failure_triggered = false;
  allocation_armed = true;
}
}
void* operator new(size_t size) { return Allocate(size); }
void* operator new[](size_t size) { return Allocate(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }

using artdcevm::evolution::ClassSchema;
using artdcevm::evolution::Member;
using artdcevm::evolution::ReadDexSchema;
namespace {
bool SameMembers(const std::vector<Member>& a, const std::vector<Member>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i=0;i<a.size();++i) {
    if (a[i].name != b[i].name || a[i].descriptor != b[i].descriptor ||
        a[i].access != b[i].access || a[i].dex_index != b[i].dex_index) return false;
  }
  return true;
}
bool SameSchema(const ClassSchema& a, const ClassSchema& b) {
  return a.descriptor == b.descriptor && a.superclass == b.superclass &&
      a.interfaces == b.interfaces && a.access == b.access &&
      SameMembers(a.fields,b.fields) && SameMembers(a.methods,b.methods);
}
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1],std::ios::binary);
  assert(input.good());
  const std::vector<unsigned char> dex{
      std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  const ClassSchema sentinel{
      "Loom/SentinelClass;", "Loom/SentinelParent;", {"Loom/SentinelInterface;"},
      0x1234, {{"sentinelField", "Ljava/lang/String;", 0x42, 99}},
      {{"sentinelMethod", "(Ljava/lang/String;)V", 0x81, 77}}};
  ClassSchema expected = sentinel;
  std::string error = "unchanged error sentinel";
  Arm(std::numeric_limits<size_t>::max());
  const bool valid = ReadDexSchema(dex,&expected,&error);
  allocation_armed = false;
  const size_t count = allocations;
  assert(valid && error.empty() && count > 0);
  assert(!expected.fields.empty() && !expected.methods.empty());
  assert(!SameSchema(expected,sentinel));

  for (size_t index=0;index<count;++index) {
    ClassSchema output = sentinel;
    error = "unchanged error sentinel";
    bool propagated = false;
    Arm(index);
    try {
      ReadDexSchema(dex,&output,&error);
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    allocation_armed = false;
    assert(failure_triggered && propagated);
    assert(SameSchema(output,sentinel));
    assert(error == "unchanged error sentinel");
    // A rejected allocation must leave no parser state that poisons a retry.
    assert(ReadDexSchema(dex,&output,&error));
    assert(SameSchema(output,expected) && error.empty());
  }
  std::printf("DEX parser OOM: %zu allocation failures propagated; output/error unchanged; retries passed\n",count);
}

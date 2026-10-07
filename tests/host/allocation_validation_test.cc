#include "transaction_metadata_profile.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/allocation_validation.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unordered_map>
#include <sys/mman.h>

namespace validation = artdcevm::android13;
namespace abi = validation::allocation_abi;
namespace {
struct RefArray { uint32_t klass = 0, monitor = 0; int32_t length = 0; uint32_t elements[4]{}; };
struct PtrArray { uint32_t klass = 0, monitor = 0; int32_t length = 0; uint32_t pad = 0; uint64_t methods[4]{}; };
struct Instance { uint32_t klass = 0, monitor = 0; };
using Class = validation::validation_abi::ClassPrefix;
using Method = validation::art_method_abi::ArtMethod;
std::unordered_map<uint32_t, void*> references;
uint32_t next_reference = 1;
bool allocation_ok = true;
size_t allocations = 0, errors = 0, heap_reads = 0;
int last_code = 0;
std::string last_error;
void* expected_definition;
uint32_t Reference(void* object) {
  if (!object) return 0;
  for (const auto& entry : references) if (entry.second == object) return entry.first;
  references[next_reference] = object;
  return next_reference++;
}
bool Original(void* definition, void*) {
  assert(definition == expected_definition);
  ++allocations;
  return allocation_ok;
}
bool Apply(void* definition, void* data, bool evolution = true) {
  expected_definition = definition;
  return validation::CollectAndValidateInstances(evolution, definition, data, Original);
}
}
namespace artdcevm::android13 {
void* ReadHeapReference(void* object, size_t offset, bool is_volatile) {
  assert(!is_volatile);
  ++heap_reads;
  uint32_t reference;
  std::memcpy(&reference, static_cast<unsigned char*>(object) + offset, sizeof(reference));
  if (!reference) return nullptr;
  assert(references.count(reference));
  return references.at(reference);
}
void RecordRedefinitionFailure(void* definition, int code, const char* message) {
  assert(definition == expected_definition);
  ++errors; last_code = code; last_error = message;
}
}
class Hooks:public artdcevm::HookEngine {public:
 bool Initialize(std::string*)override{return true;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}
 static void* Root(void* p){return references.at(*static_cast<uint32_t*>(p));}
 void* FindSymbol(const char*,const char* n)override{return !std::strcmp(n,"artReadBarrierForRootSlow")?reinterpret_cast<void*>(Root):nullptr;}
};
int main() {
  static_assert(offsetof(RefArray, elements) == abi::kReferenceArrayDataOffset);
  static_assert(offsetof(PtrArray, methods) == abi::kPointerArrayDataOffset);
  void* library = dlopen(nullptr, RTLD_NOW);
  assert(library);
  Hooks hooks;ConfigureTransactionReaderForTest(hooks);
  std::string error;
  assert(validation::ConfigureAllocationValidation(library, &error));
  // The one compressed root must really fit HeapReference32. Other compressed
  // references use an identity map, independent of host address placement.
  void* roots = mmap(reinterpret_cast<void*>(0x20000000), 16384, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(roots != MAP_FAILED && reinterpret_cast<uintptr_t>(roots) <= UINT32_MAX);
  const int32_t root_length = abi::kSlots * 130;
  std::memcpy(static_cast<unsigned char*>(roots) + abi::kArrayLengthOffset, &root_length, 4);
  uint32_t root_reference = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(roots));
  uint64_t bits[3] = {1, 0, 0};
  validation::redefinition_abi::ClassRedefinition definitions[130]{};
  validation::hierarchy_allocation_abi::Vector owners{reinterpret_cast<unsigned char*>(definitions),reinterpret_cast<unsigned char*>(definitions+130),reinterpret_cast<unsigned char*>(definitions+130)};
  abi::Holder holder{&root_reference, &owners, {bits,130,3}, {bits,130,3}, {bits, 130, 3}};
  abi::Iterator iterator{0, &holder};
  int definition;
  Class old_base{}, old_child{}, new_base{}, new_child{}, unrelated{};
  Class object{},array_type{},long_type{},long_array_type{};
  array_type.component_type=Reference(&object);long_type.primitive_type=6;long_array_type.component_type=Reference(&long_type);
  *static_cast<uint32_t*>(roots)=Reference(&array_type);
  Instance direct{Reference(&old_base)}, child{Reference(&old_child)}, other{Reference(&unrelated)};
  Method concrete{}, abstract{}; abstract.access_flags = abi::kAbstractFlag;
  PtrArray vtable{};vtable.klass=Reference(&long_array_type); vtable.length = 2;
  vtable.methods[0] = reinterpret_cast<uintptr_t>(&concrete);
  vtable.methods[1] = reinterpret_cast<uintptr_t>(&abstract);
  new_base.vtable = Reference(&vtable);
  RefArray instances{}, old_classes{}, new_classes{};
  instances.length = 1; instances.elements[0] = Reference(&direct);
  old_classes.length = new_classes.length = 2;
  old_classes.elements[0] = Reference(&old_base); old_classes.elements[1] = Reference(&old_child);
  new_classes.elements[0] = Reference(&new_base); new_classes.elements[1] = Reference(&new_child);
  auto slots = [&](int32_t index) {
    auto* elements = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(roots) + abi::kReferenceArrayDataOffset);
    elements[abi::kSlots * index + abi::kOldInstancesSlot] = Reference(&instances);
    elements[abi::kSlots * index + abi::kOldClassesSlot] = Reference(&old_classes);
    elements[abi::kSlots * index + abi::kNewClassesSlot] = Reference(&new_classes);
  };
  slots(0);
  allocation_ok = false;
  assert(!Apply(&definition, reinterpret_cast<void*>(1)));
  assert(errors == 0 && heap_reads == 0);
  allocation_ok = true;
  assert(Apply(&definition, reinterpret_cast<void*>(1), false));
  assert(errors == 0 && heap_reads == 0);
  iterator.index = 1;
  assert(Apply(&definition, &iterator)); // no initial structural allocation
  iterator.index = 0;
  assert(Apply(&definition, &iterator)); // concrete replacement skips abstract vtable
  new_base.access_flags = abi::kAbstractFlag;
  assert(!Apply(&definition, &iterator)); // direct live instance inherits abstract method
  assert(last_code == 71 && last_error == "A direct instance would inherit an abstract method");
  const size_t rejected = errors;
  instances.length = 0;
  assert(Apply(&definition, &iterator)); // no direct instance exists
  instances.length = 1; instances.elements[0] = Reference(&child);
  assert(Apply(&definition, &iterator)); // concrete subclass survives abstract parent
  instances.elements[0] = Reference(&other);
  assert(Apply(&definition, &iterator)); // an unmatched old identity does not inherit the parent
  instances.elements[0] = Reference(&direct);
  abstract.access_flags = 0;
  assert(Apply(&definition, &iterator)); // abstract class with concrete virtual implementation
  new_base.vtable = 0;
  assert(Apply(&definition, &iterator)); // no external vtable: source length is zero
  new_base.vtable = Reference(&vtable); abstract.access_flags = abi::kAbstractFlag;
  instances.length = 2; instances.elements[0] = Reference(&child); instances.elements[1] = Reference(&direct);
  assert(!Apply(&definition, &iterator)); // a later direct instance still rejects
  assert(errors == rejected + 1);
  holder.initialized.capacity=2;
  assert(!Apply(&definition, &iterator));assert(last_code==113);
  holder.initialized.capacity=3;
  new_base.component_type=Reference(&object);instances.length=0;
  assert(!Apply(&definition,&iterator)&&last_code==113); // invalid abstract array even with no direct instances
  new_base.component_type=0;
  instances.length = 0;
  for (int32_t index : {65, 128}) {
    iterator.index = index; bits[index / 64] |= uint64_t{1} << (index % 64); slots(index);
    assert(Apply(&definition, &iterator)); // correct word/bit position beyond first word
  }
  iterator.index = 130;
  assert(!Apply(&definition, &iterator) && last_code == 113);
  iterator.index = 0;
  new_classes.length = 1;
  assert(!Apply(&definition, &iterator) && last_code == 113);
  new_classes.length = 2;
  assert(Apply(&definition, &iterator));
  assert(munmap(roots, 16384) == 0);
  dlclose(library);
  std::printf("allocation validation: %zu device allocation delegations, owned bounds/direct/inherited/empty/rollback cases passed\n", allocations);
}

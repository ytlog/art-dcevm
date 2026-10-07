#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/loaded_class_guards_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata.h"
#include "platform/android13/modifier_evolution_policy.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <vector>
static bool forbid_allocation = false;
void* operator new(size_t size) { assert(!forbid_allocation); if (auto* value=std::malloc(size)) return value; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
using namespace artdcevm;
using Class = android13::validation_abi::ClassPrefix;
using Method = android13::art_method_abi::ArtMethod;
using Action = android13::ModifierEvolutionAction;
namespace {
struct Methods { uint32_t count=1,padding=0; Method values[3]{}; };
static_assert(offsetof(Methods,values)==8);
void* refs[32]{};
Class root{},base{},mid{},child{},unrelated{},iface{},implementing{};
void* targets[8]{}; size_t count=0,visits=0,root_reads=0,package_calls=0;
bool package_allowed=false;
android13::ValidationMetadata metadata{};
void* const receiver = reinterpret_cast<void*>(0x3456);
void Visit(void* linker,android13::loaded_class_abi::ClassVisitor* visitor) {
  assert(linker==receiver); ++visits; forbid_allocation=true;
  for (size_t i=0;i<count;++i) if (!(*visitor)(reinterpret_cast<uintptr_t>(targets[i]))) break;
  forbid_allocation=false;
}
bool Package(void* subtype,uintptr_t target) { assert(subtype==&child && target==reinterpret_cast<uintptr_t>(&base)); ++package_calls; return package_allowed; }
void* Root(void* method) { ++root_reads; uint32_t ref; std::memcpy(&ref,method,4); assert(ref<32); return refs[ref]; }
class Hooks : public HookEngine {
 public:
  bool missing=false;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* lib,const char* name) override {
    assert(std::strcmp(lib,"libart.so")==0);
    if(std::strcmp(name,"_ZN3art11ClassLinker12VisitClassesEPNS_12ClassVisitorE")==0) return reinterpret_cast<void*>(Visit);
    if(std::strcmp(name,"_ZN3art6mirror5Class15IsInSamePackageENS_6ObjPtrIS1_EE")==0) return reinterpret_cast<void*>(Package);
    assert(std::strcmp(name,"artReadBarrierForRootSlow")==0); return missing?nullptr:reinterpret_cast<void*>(Root);
  }
  void* Install(void*,void*,void**,std::string*) override { std::abort(); }
  bool Remove(void*,std::string*) override { std::abort(); }
};
uint32_t U32(const std::vector<unsigned char>& bytes,size_t at) { uint32_t value; std::memcpy(&value,bytes.data()+at,4); return value; }
void Put(std::vector<unsigned char>& bytes,size_t at,uint32_t value) { std::memcpy(bytes.data()+at,&value,4); }
void Loaded(Class& klass,uint32_t parent) { klass.super_class=parent; klass.status=5u<<28; }
}
namespace artdcevm::android13 {
void* GetValidationClass(void* definition) { return definition; }
bool ReadValidationClassMetadata(void* klass,ValidationMetadata* output) { assert(klass && klass!=&root); *output=metadata; return true; }
void* ReadHeapReference(void* object,size_t at,bool is_volatile) { assert(!is_volatile); uint32_t ref; std::memcpy(&ref,static_cast<unsigned char*>(object)+at,4); assert(ref<32); return refs[ref]; }
}
int main(int argc,char** argv) {
  assert(argc==2); std::ifstream input(argv[1],std::ios::binary); assert(input.good());
  std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  evolution::ClassSchema schema; std::string error; assert(evolution::ReadDexDeclarations(bytes,&schema,&error));
  const evolution::Member* selected=nullptr;
  for(const auto& candidate:schema.methods) if(candidate.name!="<init>" && candidate.name!="<clinit>") { selected=&candidate; break; }
  assert(selected); const char* name=selected->name.c_str(); const char* signature=selected->descriptor.c_str();
  metadata={{bytes.data(),bytes.size(),bytes.data(),bytes.size(),0},1};
  refs[1]=&root; refs[2]=&base; refs[3]=&mid; refs[4]=&child; refs[5]=&unrelated; refs[6]=&iface;
  Loaded(root,0); Loaded(base,1); Loaded(mid,2); Loaded(child,3); Loaded(unrelated,1); Loaded(iface,1); Loaded(implementing,1);
  Methods child_methods,mid_methods;
  child.methods=reinterpret_cast<uintptr_t>(&child_methods); mid.methods=reinterpret_cast<uintptr_t>(&mid_methods);
  child_methods.values[0].declaring_class=4; child_methods.values[0].dex_method_index=selected->dex_index;
  mid_methods.values[0].declaring_class=3; mid_methods.values[0].dex_method_index=selected->dex_index;
  Hooks hooks; hooks.missing=true; assert(!android13::ConfigureLoadedClassGuards(hooks,receiver,&error));
  hooks.missing=false; assert(!android13::ConfigureLoadedClassGuards(hooks,nullptr,&error));
  assert(android13::ConfigureLoadedClassGuards(hooks,receiver,&error));
  targets[0]=&base; targets[1]=&unrelated; count=2;
  assert(android13::CheckLoadedSubclassGuard(&base));
  targets[2]=&child; count=3; assert(!android13::CheckLoadedSubclassGuard(&base));
  child.status=4u<<28; assert(android13::CheckLoadedSubclassGuard(&base)); child.status=5u<<28;
  count=1; targets[0]=&child;
  for (const auto& candidate : schema.methods) {
    child_methods.values[0].dex_method_index=candidate.dex_index;
    assert(!android13::CheckMethodDependentGuard(&base,candidate.name.c_str(),candidate.descriptor.c_str(),1,
        static_cast<int>(Action::kCheckFinalOverride)));
  }
  child_methods.values[0].dex_method_index=selected->dex_index;
  auto check=[&](uint32_t flags,Action action,const char* sig=nullptr) { return android13::CheckMethodDependentGuard(&base,name,sig?sig:signature,flags,static_cast<int>(action)); };
  assert(!check(1,Action::kCheckFinalOverride));
  assert(check(1,Action::kCheckFinalOverride,"()Linvalid/Return;"));
  assert(check(2,Action::kCheckFinalOverride));
  assert(!check(4,Action::kCheckFinalOverride)); // protected access through ancestry
  assert(check(0,Action::kCheckFinalOverride)); assert(package_calls==1);
  package_allowed=true; assert(!check(0,Action::kCheckFinalOverride)); assert(package_calls==2);
  child_methods.values[0].declaring_class=6; // copied interface declaration still blocks final
  assert(!check(1,Action::kCheckFinalOverride));
  mid_methods.values[0].access_flags=0x400;
  assert(!check(1,Action::kCheckAbstractDependents)); // copied method isn't a concrete class declaration
  mid_methods.values[0].access_flags=1; assert(check(1,Action::kCheckAbstractDependents));
  child_methods.values[0].declaring_class=4; child_methods.values[0].access_flags=0x400;
  assert(check(1,Action::kCheckAbstractDependents)); // existing semantics continue past abstract child declaration
  mid_methods.values[0].access_flags=0x400; assert(!check(1,Action::kCheckAbstractDependents));
  child.access_flags=0x400; assert(check(1,Action::kCheckAbstractDependents)); child.access_flags=0;
  child_methods.count=2; child.virtual_methods_offset=1; child_methods.values[1]=child_methods.values[0];
  child_methods.values[0].access_flags=1; child_methods.values[1].dex_method_index=UINT32_MAX;
  child_methods.values[1].declaring_class=0; // runtime methods need no declaring DEX
  assert(check(1,Action::kCheckFinalOverride)); // direct prefix and runtime signature excluded
  child.virtual_methods_offset=0; assert(!check(1,Action::kCheckFinalOverride));
  // IfTable includes transitively implemented interfaces; entries are reference pairs.
  struct Table { uint32_t klass=0,monitor=0; int32_t length=2; uint32_t values[2]{6,0}; } table;
  refs[7]=&table; implementing.iftable=7; iface.access_flags=0x200; targets[0]=&implementing;
  assert(!android13::CheckLoadedSubclassGuard(&iface)); table.length=0; assert(android13::CheckLoadedSubclassGuard(&iface));
  // Primitive classes don't count as loaded subclasses of Object.
  targets[0]=&unrelated; unrelated.primitive_type=5; assert(android13::CheckLoadedSubclassGuard(&root)); unrelated.primitive_type=0;
  assert(!android13::CheckLoadedSubclassGuard(&root));
  // Compact DEX ID tables and proto/type-list/string data may be separate mappings.
  size_t data_start=U32(bytes,108); std::vector<unsigned char> compact(bytes.begin(),bytes.begin()+data_start);
  compact.insert(compact.begin()+0x70,24,0); std::memcpy(compact.data(),"cdex001\0",8); Put(compact,36,0x88);
  for(size_t header:{size_t{56},size_t{64},size_t{72},size_t{80},size_t{88},size_t{96}})
    if(U32(compact,header)) Put(compact,header+4,U32(compact,header+4)+24);
  metadata.dex.begin=compact.data(); metadata.dex.size=compact.size(); targets[0]=&child;
  assert(!check(1,Action::kCheckFinalOverride));
  assert(visits>20 && root_reads>10);
  std::cout<<"loaded-class guards: fixed visitor ABI, real DEX/shared CompactDex signatures, loaded/final/abstract, copied/direct/runtime methods, ancestor semantics, access/package, interface/root criteria and allocation-free visits passed\n";
}

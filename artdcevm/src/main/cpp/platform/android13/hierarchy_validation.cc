#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/hierarchy_allocation.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/validation_metadata.h"
#include "platform/symbols/android13/source_symbols.h"
#include <cstring>
#include <jvmti.h>
#include <limits>
namespace artdcevm::android13 {
namespace {
thread_local uint32_t state=0;
constexpr uint32_t kChanged=1,kNarrowed=2;
using FindClass=uintptr_t(*)(void*,void*,const char*,void*);
FindClass find_class=nullptr;
using Class=validation_abi::ClassPrefix;
template<class T> T Read(void* object,size_t offset) { T value;std::memcpy(&value,static_cast<unsigned char*>(object)+offset,sizeof(value));return value; }
uint32_t Flags(void* klass) { return Read<uint32_t>(klass,offsetof(Class,access_flags)); }
void* Parent(void* klass) { return ReadHeapReference(klass,offsetof(Class,super_class)); }
bool Initialized(void* klass) {
  return (__atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<unsigned char*>(klass)+offsetof(Class,status)),__ATOMIC_SEQ_CST)>>28)>=14;
}
}
uint32_t BeginHierarchyTransaction() { const uint32_t previous=state;state=0;return previous; }
void EndHierarchyTransaction(uint32_t previous) { state=previous; }
bool HasHierarchyChange() { return (state&kChanged)!=0; }
bool HasHierarchyNarrowing() { return (state&kNarrowed)!=0; }
int CheckEvolutionHierarchy(void* definition,const evolution::ClassSchema& current,
    const evolution::ClassSchema& replacement,bool* changed,std::string* error) {
  auto* self=CurrentHierarchyThread();
  auto fail=[&](const char* message) {
    ClearHierarchyException(self);*error=message;return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  };
  if(!self || !RuntimeClassLinker()) { *error="Missing hierarchy validation thread or class linker";return JVMTI_ERROR_INTERNAL; }
  if(replacement.interfaces.size()>static_cast<size_t>(INT32_MAX)-3) {
    *error="Too many hierarchy interface roots";return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  // Current class, loader, selected parent and every direct interface remain
  // rooted across FindClass, which can allocate, suspend and relocate all roots.
  HandleRootScope roots(self,static_cast<uint32_t>(replacement.interfaces.size()+3));
  roots.Set(0,GetValidationClass(definition));
  if(!roots.Get(0)) { *error="Missing hierarchy validation class";return JVMTI_ERROR_INTERNAL; }
  if((Read<uint32_t>(roots.Get(0),offsetof(Class,primitive_type))&0xffff)==0 && !Parent(roots.Get(0)))
    return fail("The Object root cannot evolve its hierarchy");
  if(replacement.superclass.empty()) return fail("Missing superclass");
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(Class,class_loader)));
  *changed=current.superclass!=replacement.superclass;
  roots.Set(2,reinterpret_cast<void*>(find_class(RuntimeClassLinker(),self,replacement.superclass.c_str(),roots.Slot(1))));
  if(!roots.Get(2)) return fail("Cannot resolve requested superclass");
  if((Flags(roots.Get(2))&(0x200|0x10)) || !CanAccessValidationClass(roots.Get(0),roots.Get(2)))
    return fail("Invalid or inaccessible superclass");
  if(IsValidationSubclassOf(roots.Get(2),roots.Get(0))) return fail("Cyclic superclass change");
  const bool loses_parent=!IsValidationSubclassOf(roots.Get(2),Parent(roots.Get(0)));
  // Receiver rejection alone cannot keep ancestor-typed fields, static cells
  // and arrays assignable. This retired route has no same-plan stopped whole-
  // heap storage proof; only the common full-schema owner can authorize loss.
  if(loses_parent) return fail("Legacy hierarchy narrowing lacks stopped typed-storage proof");
  if(*changed && !Initialized(roots.Get(2))) return fail("Initialize the added superclass before applying this evolution");
  for(size_t i=0;i<replacement.interfaces.size();++i) {
    roots.Set(3+i,reinterpret_cast<void*>(find_class(RuntimeClassLinker(),self,replacement.interfaces[i].c_str(),roots.Slot(1))));
    if(!roots.Get(3+i) || !(Flags(roots.Get(3+i))&0x200) ||
        !CanAccessValidationClass(roots.Get(0),roots.Get(3+i))) return fail("Invalid or inaccessible interface");
  }
  auto* table=ReadHeapReference(roots.Get(0),offsetof(Class,iftable));
  const int32_t count=table?Read<int32_t>(table,8)/2:0;
  for(int32_t i=0;i<count;++i) {
    auto* old_interface=ReadHeapReference(table,12+static_cast<size_t>(i)*8);
    bool retained=IsValidationAssignableFrom(old_interface,roots.Get(2));
    for(size_t j=0;j<replacement.interfaces.size();++j)
      retained|=IsValidationAssignableFrom(old_interface,roots.Get(3+j));
    if(!retained) return fail("Legacy interface narrowing lacks stopped typed-storage proof");
  }
  *changed|=current.interfaces!=replacement.interfaces;
  if(*changed) state|=kChanged;
  if(*changed && !HasHierarchyAllocationContext()) { *error="The device transaction hook did not establish allocation context";return JVMTI_ERROR_NOT_AVAILABLE; }
  return JVMTI_ERROR_NONE;
}
bool ConfigureHierarchyValidation(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()){*error="Hierarchy validation requires a matched device profile";return false;}
  auto find=reinterpret_cast<FindClass>(hooks.FindSymbol("libart.so",::artdcevm::symbols::android13::kFindClass));
  if(!find || !RuntimeClassLinker()) {
    *error="Missing named hierarchy resolver or class linker";return false;
  }
  if(!ConfigureHandleRootScopes(hooks,profile,error)) return false;
  find_class=find;
  return true;
}
}

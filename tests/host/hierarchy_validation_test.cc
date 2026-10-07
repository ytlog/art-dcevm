#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/hierarchy_validation_abi.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/handle_root_scope_abi.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/validation_metadata.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <jvmti.h>
#include <thread>
#include <sys/mman.h>
using namespace artdcevm;
using Class=android13::validation_abi::ClassPrefix;
using Header=android13::handle_root_abi::ScopeHeader;
namespace {
constexpr size_t kNodes=11;
enum Id { Object,Base,OldParent,NewParent,OtherParent,Child,OldIface,NewIface,Loader,BaseTable,InterfaceTable };
const char* names[kNodes]{"Ljava/lang/Object;","Ltest/Base;","Ltest/OldParent;","Ltest/NewParent;","Ltest/OtherParent;","Ltest/Child;","Ltest/OldIface;","Ltest/NewIface;","Ltest/Loader;","",""};
void* nodes[kNodes]{};
unsigned char* arena=nullptr; size_t used=0,gcs=0,finds=0;
alignas(8) unsigned char thread_bytes[320]{};
void* const linker=reinterpret_cast<void*>(0x777);
bool guard=false,context=true,package=true,gc_enabled=true;
Id subject=Base;
android13::BeginHierarchyState source_begin=nullptr;
android13::EndHierarchyState source_end=nullptr;
android13::HierarchyStateQuery source_changed=nullptr,source_narrowed=nullptr;
template<class T> T Read(void* ptr,size_t at=0) { T value;std::memcpy(&value,static_cast<unsigned char*>(ptr)+at,sizeof(value));return value; }
template<class T> void Put(void* ptr,size_t at,T value) { std::memcpy(static_cast<unsigned char*>(ptr)+at,&value,sizeof(value)); }
uint32_t Ref(void* value) { auto v=reinterpret_cast<uintptr_t>(value);assert(v<=UINT32_MAX);return static_cast<uint32_t>(v); }
void* Fresh() { assert(used+128<1024*1024);void* value=arena+used;used+=128;std::memset(value,0,128);return value; }
size_t Index(void* object) { for(size_t i=0;i<kNodes;++i)if(nodes[i]==object)return i;assert(false && "read of stale pre-GC pointer");return 0; }
Class* Klass(Id id) { return static_cast<Class*>(nodes[id]); }
void* Top() { return Read<void*>(thread_bytes,android13::handle_root_abi::kTopHandleScopeOffset); }
void Move() {
  ++gcs;void* old[kNodes];std::memcpy(old,nodes,sizeof(nodes));
  for(size_t i=0;i<kNodes;++i) { nodes[i]=Fresh();std::memcpy(nodes[i],old[i],128); }
  auto translate=[&](uint32_t value) { for(size_t i=0;i<kNodes;++i)if(value==Ref(old[i]))return Ref(nodes[i]);return value; };
  for(size_t i=0;i<BaseTable;++i) {
    auto* klass=static_cast<Class*>(nodes[i]);
    klass->super_class=translate(klass->super_class);klass->class_loader=translate(klass->class_loader);
    klass->iftable=translate(klass->iftable);klass->component_type=translate(klass->component_type);
  }
  for(size_t i=BaseTable;i<kNodes;++i) {
    int32_t length=Read<int32_t>(nodes[i],8);
    for(int32_t j=0;j<length;++j) Put<uint32_t>(nodes[i],12+static_cast<size_t>(j)*4,translate(Read<uint32_t>(nodes[i],12+static_cast<size_t>(j)*4)));
  }
  for(void* scope=Top();scope;scope=Read<void*>(scope)) {
    auto header=Read<Header>(scope);assert(header.count>=0);
    for(int32_t i=0;i<header.count;++i) {
      size_t at=12+static_cast<size_t>(i)*4;Put<uint32_t>(scope,at,translate(Read<uint32_t>(scope,at)));
    }
  }
  for(auto* value:old)std::memset(value,0xa5,128);
}
void* Current() { return thread_bytes; }
uintptr_t Find(void* receiver,void* self,const char* descriptor,void* loader_slot) {
  assert(receiver==linker && self==thread_bytes && Top());++finds;
  assert(Read<uint32_t>(loader_slot)==Ref(nodes[Loader]));
  if(gc_enabled)Move();
  assert(Read<uint32_t>(loader_slot)==Ref(nodes[Loader]));
  for(size_t i=0;i<BaseTable;++i)if(std::strcmp(names[i],descriptor)==0)return reinterpret_cast<uintptr_t>(nodes[i]);
  Put<void*>(thread_bytes,android13::handle_root_abi::kExceptionOffset,reinterpret_cast<void*>(0x1234));return 0;
}
bool SamePackage(void* first,uintptr_t second) { Index(first);Index(reinterpret_cast<void*>(second));return package; }
void Visit(void*,void*) { std::abort(); }
void* ReadRoot(void*) { std::abort(); }
class Hooks:public HookEngine {
 public:
  bool missing=false;
  bool Initialize(std::string*) override{return true;}
  void* FindSymbol(const char* library,const char* symbol) override {
    assert(std::strcmp(library,"libart.so")==0);
    if(std::strstr(symbol,"CurrentFromGdb"))return missing?nullptr:reinterpret_cast<void*>(Current);
    if(std::strstr(symbol,"9FindClass"))return reinterpret_cast<void*>(Find);
    if(std::strstr(symbol,"IsInSamePackage"))return reinterpret_cast<void*>(SamePackage);
    if(std::strstr(symbol,"VisitClasses"))return reinterpret_cast<void*>(Visit);
    assert(std::strcmp(symbol,"artReadBarrierForRootSlow")==0);return reinterpret_cast<void*>(ReadRoot);
  }
  void* Install(void*,void*,void**,std::string*) override{std::abort();}
  bool Remove(void*,std::string*) override{std::abort();}
};
}
namespace artdcevm::android13 {
void* RuntimeClassLinker(){return linker;}
void* GetValidationClass(void*){return nodes[subject];}
bool ReadValidationClassMetadata(void*,ValidationMetadata*){std::abort();}
void* ReadHeapReference(void* object,size_t at,bool is_volatile){assert(!is_volatile);Index(object);return reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(object,at)));}
}
namespace artdcevm::android13 {
bool ReceiverGuardEnabled(){return guard;}
bool HasHierarchyAllocationContext(){return context;}
}
extern "C" void ArtDcevmSourceConfigureHierarchyState(android13::BeginHierarchyState begin,android13::EndHierarchyState end,
    android13::HierarchyStateQuery changed,android13::HierarchyStateQuery narrowed){source_begin=begin;source_end=end;source_changed=changed;source_narrowed=narrowed;}
extern "C" int ArtDcevmSourceCalibrateHandleRootScopes(int(*calibrate)(void*)){return calibrate(thread_bytes);}
extern "C" int ArtDcevmSourceProbeHandleRootScope(void* thread,void* scope,uint32_t count){
  assert(thread==thread_bytes);if(!scope)return 0;assert(Top()==scope);auto h=Read<Header>(scope);assert(h.count==static_cast<int32_t>(count));
  for(size_t i=0;i<count;++i)assert(Read<uint32_t>(scope,12+i*4)==0);return 0;
}
int main(){
  arena=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
  assert(arena!=MAP_FAILED && reinterpret_cast<uintptr_t>(arena)+1024*1024<=UINT32_MAX);
  for(auto& node:nodes)node=Fresh();
  for(size_t i=0;i<BaseTable;++i){auto* klass=static_cast<Class*>(nodes[i]);klass->status=14u<<28;klass->access_flags=1;klass->class_loader=Ref(nodes[Loader]);klass->super_class=i==Object?0:Ref(nodes[Object]);}
  Klass(Base)->super_class=Ref(nodes[OldParent]);Klass(NewParent)->super_class=Ref(nodes[OldParent]);Klass(Child)->super_class=Ref(nodes[Base]);
  Klass(OldIface)->access_flags=Klass(NewIface)->access_flags=0x201;
  Klass(Base)->iftable=Ref(nodes[BaseTable]);Klass(NewIface)->iftable=Ref(nodes[InterfaceTable]);
  Put<int32_t>(nodes[BaseTable],8,2);Put<uint32_t>(nodes[BaseTable],12,Ref(nodes[OldIface]));
  Put<int32_t>(nodes[InterfaceTable],8,2);Put<uint32_t>(nodes[InterfaceTable],12,Ref(nodes[OldIface]));
  Hooks hooks;std::string error;void* implementation=dlopen(nullptr,RTLD_NOW);assert(implementation);
  assert(android13::ConfigureLoadedClassGuards(hooks,linker,&error));hooks.missing=true;
  assert(!android13::ConfigureHierarchyValidation(hooks,implementation,&error));hooks.missing=false;
  assert(android13::ConfigureHierarchyValidation(hooks,implementation,&error));assert(!Top());
  assert(source_begin && source_end && source_changed && source_narrowed);
  {
    android13::HandleRootScope outer(thread_bytes,2);outer.Set(0,nodes[Base]);outer.Set(1,nodes[Loader]);
    {android13::HandleRootScope inner(thread_bytes,1);inner.Set(0,nodes[OldIface]);Move();assert(inner.Get(0)==nodes[OldIface]);assert(outer.Get(0)==nodes[Base]);assert(outer.Get(1)==nodes[Loader]);}
    assert(Top()==outer.NativeScope());
  }assert(!Top());
  evolution::ClassSchema current;current.descriptor=names[Base];current.superclass=names[OldParent];current.interfaces={names[OldIface]};
  auto replacement=current;bool changed=false;
  auto check=[&](int expected,const char* diagnostic=nullptr){error.clear();changed=false;uint32_t previous=source_begin();
    int result=android13::CheckEvolutionHierarchy(reinterpret_cast<void*>(1),current,replacement,&changed,&error);
    if(result!=expected || Top())std::cerr<<"expected="<<expected<<" actual="<<result<<" parent="<<replacement.superclass<<" error="<<error<<" top="<<Top()<<"\n";
    assert(result==expected && !Top());if(diagnostic)assert(error==diagnostic);assert(!Read<void*>(thread_bytes,android13::handle_root_abi::kExceptionOffset));
    assert(source_changed()==changed || result!=0);source_end(previous);};
  Klass(OldParent)->status=7u<<28;check(0);assert(!changed); // unchanged parent need not be initialized
  replacement.superclass=names[NewParent];replacement.interfaces={names[NewIface]};check(0);assert(changed && gcs>=4);
  Klass(NewParent)->status=13u<<28;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Initialize the added superclass before applying this evolution");Klass(NewParent)->status=14u<<28;
  replacement.superclass="Lmissing/Class;";check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Cannot resolve requested superclass");
  replacement.superclass=names[Child];check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Cyclic superclass change");
  replacement.superclass=names[NewParent];Klass(NewParent)->access_flags=0x11;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Invalid or inaccessible superclass");Klass(NewParent)->access_flags=0x201;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Invalid or inaccessible superclass");
  Klass(NewParent)->access_flags=0;package=false;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Invalid or inaccessible superclass");package=true;Klass(NewParent)->access_flags=1;
  replacement.interfaces={names[NewParent]};check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Invalid or inaccessible interface");replacement.interfaces={"Lmissing/Interface;"};check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Invalid or inaccessible interface");
  replacement.interfaces={names[NewIface]};replacement.superclass=names[OtherParent];check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Removing a superclass requires the active receiver guard");
  guard=true;uint32_t previous=source_begin();assert(android13::CheckEvolutionHierarchy(reinterpret_cast<void*>(1),current,replacement,&changed,&error)==0);assert(source_changed() && source_narrowed());
  uint32_t nested=source_begin();assert(!source_changed()&&!source_narrowed());source_end(nested);assert(source_changed()&&source_narrowed());
  std::thread isolated([]{assert(!android13::HasHierarchyChange()&&!android13::HasHierarchyNarrowing());auto prior=android13::BeginHierarchyTransaction();assert(prior==0);android13::EndHierarchyTransaction(prior);});isolated.join();source_end(previous);
  replacement.superclass=names[NewParent];replacement.interfaces.clear();guard=false;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Removing an interface requires the active receiver guard");guard=true;check(0);assert(changed);
  context=false;check(JVMTI_ERROR_NOT_AVAILABLE,"The device transaction hook did not establish allocation context");context=true;
  subject=Object;check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"The Object root cannot evolve its hierarchy");subject=Base;
  replacement.superclass.clear();check(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Missing superclass");
  assert(finds>15 && gcs>15);assert(munmap(arena,1024*1024)==0);dlclose(implementation);
  std::cout<<"hierarchy validation: moving-GC roots across every named FindClass, root-chain nesting, parent/interface/access/cycle/initialization/narrowing, exception clearing, context errors and thread-local state passed\n";
}

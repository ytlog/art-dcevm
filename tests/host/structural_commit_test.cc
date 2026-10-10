#include "transaction_metadata_profile.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/android13/structural_commit.h"
#include "platform/abi/arm64/android13/allocation_validation_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/android13/structural_reflective.h"
#include "platform/abi/arm64/android13/object_replacement_map_abi.h"
#include "platform/abi/arm64/android13/selective_deopt_abi.h"
#include "platform/android13/symbols/profile.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace artdcevm;
using namespace artdcevm::android13;
namespace {
using Class=validation_abi::ClassPrefix;
unsigned char* heap;
size_t allocated=0;
void* meta,*object_type,*array_type,*class_array_type;
void* old_class[2],*new_class[2],*old_instance,*new_instance;
void* old_classes,*new_classes,*old_instances,*new_instances,*holder_array;
unsigned char thread[4096]{};
unsigned char runtime[2048]{};
void* instance=runtime;
void* mutex=reinterpret_cast<void*>(9);
void* jit=reinterpret_cast<void*>(17);
std::vector<std::string> events;
bool locked=false,abstract_case=false;
uint32_t root_slot;
template<class T> T Read(const void* p,size_t at=0) { T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value; }
template<class T> void Write(void* p,size_t at,T value) { std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value)); }
uint32_t Ref(void* p) { assert(reinterpret_cast<uintptr_t>(p)<=UINT32_MAX);return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
void* Object() { void* p=heap+256*allocated++;std::memset(p,0,256);return p; }
void* Array(std::initializer_list<void*> list) { void* p=Object();Write<uint32_t>(p,0,Ref(array_type));Write<int32_t>(p,8,list.size());size_t i=0;for(auto item:list) Write<uint32_t>(p,12+4*i++,Ref(item));return p; }
void* At(void* array,size_t index) { return reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(array,12+4*index)}); }
uint32_t Flags(void* c) { return Read<uint32_t>(c,64); }
void SetStatus(void* c,uint8_t status) { assert(c==new_class[0] && status==14);Write<uint32_t>(c,112,uint32_t{status}<<28);events.push_back("status"); }
void Retain(void* ext,void* old) {
  size_t i=old==old_class[0]?0:1;assert(old==old_class[i]);assert(ext==reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(new_class[i],20)}));
  assert(At(old_classes,i)==new_class[i] && At(new_classes,i)==new_class[i]);events.push_back("retain"+std::to_string(i));
}
void Invalidate(void* cache) { assert(cache==jit);events.push_back("jit"); }
void Deopt(void* instrumentation,const char* reason) {
  assert(instrumentation==runtime+offsetof(selective_abi::RuntimePrefix,instrumentation));
  assert(abstract_case && std::strcmp(reason,"artdcevm-class-abstract")==0);events.push_back("abstract");
}
void Lock(void* m,void* self) { assert(m==mutex && self==thread && !locked);locked=true;events.push_back("lock"); }
void Unlock(void* m,void* self) { assert(m==mutex && self==thread && locked);locked=false;events.push_back("unlock"); }
void Each(void* list,void(*callback)(void*,void*),void* context) {
  assert(list==reinterpret_cast<void*>(31) && locked);events.push_back("threads");callback(thread,context);
}
void Clear(void* cache,void* self) { assert(locked && self==thread && cache==thread+2608);events.push_back("clear"); }
void Replace(void* self,const void* raw) {
  assert(self==thread);events.push_back("replace");auto& map=*static_cast<const object_map_abi::Map*>(raw);
  assert(map.size==3);std::vector<std::pair<void*,void*>> pairs;
  for(auto* base=map.sentinel.next;base;base=base->next) {auto* node=static_cast<object_map_abi::Node*>(base);pairs.emplace_back(node->key,node->value);}
  auto swap=[&](uint32_t reference) { for(auto [old,fresh]:pairs) if(reference==Ref(old)) return Ref(fresh);return reference; };
  // Model ART's heap and holder-root rewrite, without touching native vectors.
  for(void* array:{old_classes,new_classes,old_instances,new_instances,holder_array})
    for(int32_t i=0;i<Read<int32_t>(array,8);++i) Write<uint32_t>(array,12+4*i,swap(Read<uint32_t>(array,12+4*i)));
  Write<uint32_t>(old_instance,0,swap(Read<uint32_t>(old_instance)));
  assert(At(holder_array,4)==new_class[0]);
}
struct Hooks:HookEngine {
 const char* missing=nullptr;
 bool Initialize(std::string*) override {return true;}
 void* FindSymbol(const char*,const char* name) override {
  if(missing&&std::strstr(name,missing))return nullptr;
  if(std::strcmp(name,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(+[](void* p)->void*{return reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(p)));});
  if(std::strcmp(name,"_ZN3art7Runtime9instance_E")==0) return &instance;
  if(std::strcmp(name,"_ZN3art5Locks17thread_list_lock_E")==0) return &mutex;
  if(std::strstr(name,"SetStatusLocked")) return reinterpret_cast<void*>(SetStatus);
  if(std::strstr(name,"SetObsoleteClass")) return reinterpret_cast<void*>(Retain);
  if(std::strcmp(name,cas::kInvalidateAllCompiledCode)==0) return reinterpret_cast<void*>(Invalidate);
  if(std::strstr(name,"DeoptimizeEverything")) return reinterpret_cast<void*>(Deopt);
  if(std::strstr(name,"ExclusiveUnlock")) return reinterpret_cast<void*>(Unlock);
  if(std::strstr(name,"ExclusiveLock")) return reinterpret_cast<void*>(Lock);
  if(std::strstr(name,"ThreadList7ForEach")) return reinterpret_cast<void*>(Each);
  if(std::strstr(name,"InterpreterCache5Clear")) return reinterpret_cast<void*>(Clear);
  if(std::strstr(name,"ReplaceReferences")) return reinterpret_cast<void*>(Replace);
  return nullptr;
 }
 void* Install(void*,void*,void**,std::string*) override {return nullptr;}
 bool Remove(void*,std::string*) override {return true;}
};
void Setup(bool make_abstract) {
  events.clear();locked=false;allocated=0;abstract_case=make_abstract;
  meta=Object();object_type=Object();array_type=Object();class_array_type=Object();
  Write<uint32_t>(meta,0,Ref(meta));Write<uint32_t>(array_type,offsetof(Class,component_type),Ref(object_type));Write<uint32_t>(class_array_type,offsetof(Class,component_type),Ref(meta));
  for(int i=0;i<2;++i) {old_class[i]=Object();new_class[i]=Object();Write<uint32_t>(old_class[i],0,Ref(meta));Write<uint32_t>(new_class[i],0,Ref(meta));Write<uint32_t>(new_class[i],20,Ref(Object()));}
  Write<uint32_t>(old_class[0],112,14u<<28);Write<uint32_t>(new_class[0],112,10u<<28);
  Write<uint32_t>(old_class[1],112,10u<<28);Write<uint32_t>(new_class[1],112,14u<<28);
  Write<uint32_t>(old_class[0],64,1);Write<uint32_t>(new_class[0],64,make_abstract?0x401u:1u);
  old_instance=Object();new_instance=Object();Write<uint32_t>(old_instance,0,Ref(old_class[0]));Write<uint32_t>(new_instance,0,Ref(new_class[0]));
  old_classes=Array({old_class[0],old_class[1]});new_classes=Array({new_class[0],new_class[1]});
  Write<uint32_t>(old_classes,0,Ref(class_array_type));Write<uint32_t>(new_classes,0,Ref(class_array_type));
  old_instances=Array({old_instance});new_instances=Array({new_instance});holder_array=Object();Write<uint32_t>(holder_array,0,Ref(array_type));Write<int32_t>(holder_array,8,13);
  for(auto [slot,p]:std::vector<std::pair<int,void*>>{{4,old_class[0]},{9,old_instances},{10,new_instances},{11,old_classes},{12,new_classes}}) Write<uint32_t>(holder_array,12+4*slot,Ref(p));
  root_slot=Ref(holder_array);Write<void*>(runtime,offsetof(runtime_access_abi::RuntimePrefix,thread_list),reinterpret_cast<void*>(31));
}
void Commit() {redefinition_abi::ClassRedefinition owner{};hierarchy_allocation_abi::Vector owners{reinterpret_cast<unsigned char*>(&owner),reinterpret_cast<unsigned char*>(&owner+1),reinterpret_cast<unsigned char*>(&owner+1)};uint64_t bits=1;
 allocation_abi::Holder holder{&root_slot,&owners,{&bits,1,1},{&bits,1,1},{&bits,1,1}};allocation_abi::Iterator iterator{0,&holder};CommitEvolutionStructurally(nullptr,&iterator);}
}
namespace artdcevm::android13 {
void* ReadHeapReference(void* p,size_t offset,bool) {return reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(p,offset)});}
void* CurrentHierarchyThread() {return thread;}
void* RuntimeJitCodeCache() {return jit;}
void* RuntimeInstrumentation() {return runtime+offsetof(selective_abi::RuntimePrefix,instrumentation);}
bool HasHierarchyChange() {return true;}
StructuralMemberMappings BuildStructuralMemberMappings(const std::vector<void*>& old,const std::vector<void*>& fresh) {
 assert((old==std::vector<void*>{old_class[0],old_class[1]}) && (fresh==std::vector<void*>{new_class[0],new_class[1]}));events.push_back("mappings");return {};
}
void CopyEvolutionFields(bool is_static,void* target,void* target_class,void* source,void* source_class) {
 if(is_static) {size_t i=source==old_class[0]?0:1;assert(source==old_class[i] && target==new_class[i] && source_class==source && target_class==target);events.push_back("static"+std::to_string(i));}
 else {assert(source==old_instance && target==new_instance && source_class==old_class[0] && target_class==new_class[0]);events.push_back("instance");}
}
void UpdateStructuralMethods(void* old,void* fresh) {size_t i=old==old_class[0]?0:1;assert(fresh==new_class[i] && (Flags(old)&0x200000));events.push_back("methods"+std::to_string(i));}
void VisitStructuralReflectiveTargets(void* rt,void* mirror,const std::vector<void*>& old,const std::vector<void*>& fresh,const StructuralMemberMappings&,bool changed) {
 assert(rt==runtime && mirror==old_class[0] && old[0]==old_class[0] && fresh[0]==new_class[0] && changed);assert(At(old_classes,0)==old_class[0]);events.push_back("reflective");
}
void DeoptimizeApplicationFrames() {events.push_back("deopt");}
}
int main() {
 void* mapped=mmap(reinterpret_cast<void*>(0x20000000),1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mapped!=MAP_FAILED && reinterpret_cast<uintptr_t>(mapped)+1024*1024<=UINT32_MAX);heap=static_cast<unsigned char*>(mapped);
 Hooks hooks;ConfigureTransactionReaderForTest(hooks);std::string error;
 for(const char* name:{"Runtime9instance_E","thread_list_lock_E","SetStatusLocked","SetObsoleteClass","InvalidateAllCompiledCode","DeoptimizeEverything","ExclusiveLock","ExclusiveUnlock","ThreadList7ForEach","InterpreterCache5Clear","ReplaceReferences"}){hooks.missing=name;assert(!ConfigureStructuralCommit(hooks,&error)&&!error.empty());}
 hooks.missing=nullptr;assert(ConfigureStructuralCommit(hooks,&error));
 for(bool abstract:{false,true}) for(bool have_jit:{false,true}) {
  Setup(abstract);jit=have_jit?reinterpret_cast<void*>(17):nullptr;Commit();
  std::vector<std::string> expected{"mappings","status","static0","static1","instance","methods0","methods1","reflective","deopt","replace","retain0","retain1"};
  if(have_jit) expected.push_back("jit");if(abstract) expected.push_back("abstract");
  expected.insert(expected.end(),{"lock","threads","clear","unlock"});assert(events==expected);
  assert(Read<uint32_t>(new_class[0],112)>>28==14 && Read<uint32_t>(new_class[1],112)>>28==14);
 }
 // Source map[old_instance->GetClass()] also requires a class replacement.
 // Reject the invalid array relation before the first destructive copy.
 Setup(false);Write<uint32_t>(old_instance,0,Ref(Object()));pid_t child=fork();assert(child>=0);
 if(child==0) {Commit();_exit(0);}int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
 for(uint32_t erroneous:{2u,3u}) {
  Setup(false);Write<uint32_t>(old_class[0],112,erroneous<<28);
  pid_t child=fork();assert(child>=0);
  if(child==0) {Commit();_exit(0);}int status;assert(waitpid(child,&status,0)==child);
  assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
 }
 munmap(heap,1024*1024);std::cout<<"structural commit: array roots, status/copy/obsolete/reflective/deopt/replace/retain order, conditional JIT/abstract and cache locking passed\n";
}

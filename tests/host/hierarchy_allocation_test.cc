#include "transaction_metadata_profile.h"
#include "handle_scope_fixture.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/hierarchy_allocation.h"
#include "platform/android13/hierarchy_allocation_abi.h"
#include "platform/android13/handle_root_scope_abi.h"
#include "platform/android13/platform_string_abi.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/symbols/android13.h"
#include <cassert>
#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unordered_map>
using namespace artdcevm;
using namespace artdcevm::android13;
using Class=validation_abi::ClassPrefix;
using Cache=hierarchy_allocation_abi::Cache;
using TypePair=hierarchy_allocation_abi::TypePair;
using NativePair=hierarchy_allocation_abi::NativePair;
namespace {
unsigned char thread[512]{};
unsigned char* heap;
size_t generation=0;
struct Node { void* pointer; int kind; }; // class=1/cache=2/loader=3/array=4
std::vector<Node> nodes;
size_t array_type_id;
size_t object_id,b_id,c_id,d_id,loader_id,b_cache_id,c_cache_id;
std::unordered_map<std::string,size_t> names;
std::string failure;
std::vector<std::string> allocation_order;
int barriers=0,gcs=0;
bool exclusive_mutator=false;
int retire_calls=0,suspend_calls=0;
TypePair types[4]{};
alignas(16) NativePair methods[4]{},fields[3]{};
template<class T> T Read(const void* pointer,size_t at=0) { T value;std::memcpy(&value,static_cast<const unsigned char*>(pointer)+at,sizeof(value));return value; }
template<class T> void Write(void* pointer,size_t at,T value) { std::memcpy(static_cast<unsigned char*>(pointer)+at,&value,sizeof(value)); }
void* Ptr(size_t index) { return nodes.at(index).pointer; }
uint32_t Ref(void* value) { assert(reinterpret_cast<uintptr_t>(value)<=UINT32_MAX);return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)); }
size_t New(int kind) {
  size_t i=nodes.size();void* pointer=heap+generation*1024*1024+i*256;
  std::memset(pointer,0,256);nodes.push_back({pointer,kind});return i;
}
void GC() {
  assert(generation<62);++generation;++gcs;
  std::unordered_map<uint32_t,uint32_t> moved;
  for(size_t i=0;i<nodes.size();++i) {
    void* old=Ptr(i),*fresh=heap+generation*1024*1024+i*256;
    std::memcpy(fresh,old,256);moved[Ref(old)]=Ref(fresh);
    std::memset(old,0xa5,256);nodes[i].pointer=fresh;
  }
  auto move=[&](uint32_t* slot) { if(*slot) { assert(moved.count(*slot));*slot=moved.at(*slot); } };
  for(const auto& node:nodes) {
    if(node.kind==1) {
      auto* klass=static_cast<Class*>(node.pointer);
      for(uint32_t* slot:{&klass->object.klass,&klass->component_type,&klass->class_loader,&klass->super_class,&klass->dex_cache}) move(slot);
    } else if(node.kind==2) move(&static_cast<Cache*>(node.pointer)->prefix.class_loader);
    else if(node.kind==4) {
      move(static_cast<uint32_t*>(node.pointer));
      int32_t length=Read<int32_t>(node.pointer,8);
      for(int32_t j=0;j<length;++j) move(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(node.pointer)+12+j*4));
    }
  }
  for(auto& type:types) move(&type.object);
  void* scope=Read<void*>(thread,handle_root_abi::kTopHandleScopeOffset);
  while(scope) {
    auto header=Read<handle_root_abi::ScopeHeader>(scope);
    assert(header.count>=0);
    for(int32_t i=0;i<header.count;++i) move(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(scope)+12+i*4));
    scope=header.link;
  }
}
void* Current() { return thread; }
void* Peer(void*){return Ptr(object_id);}
void Runnable(void* t){*static_cast<uint32_t*>(t)=0;}
void Native(void* t){*static_cast<uint32_t*>(t)=92u<<24;}
void Suspend(void*,const char* cause,bool) {
  assert(cause && !exclusive_mutator && Read<uint32_t>(thread)==(92u<<24));
  exclusive_mutator=true;++suspend_calls;
}
void Resume(void*) {assert(exclusive_mutator);exclusive_mutator=false;}
void Retire(void* object,uint8_t status) {
  // Fixed Class::CheckSetStatus: retirement permits a backwards transition,
  // requires a resolved class under exclusive mutator access, and cannot
  // transition directly from kInitialized (14).
  auto* klass=static_cast<Class*>(object);
  unsigned old=klass->status>>28;
  assert(exclusive_mutator && old>=7 && old!=14 && status==1);
  klass->status=static_cast<uint32_t>(status)<<28;++retire_calls;
}
struct Dex {
  std::vector<unsigned char> bytes,shared_data;
  validation_abi::DexMappingPrefix mapping;
  Dex(const std::string& child,const std::string& parent,bool compact=false,bool later=false):bytes(512,0),shared_data(512,0),mapping{nullptr,bytes.data(),bytes.size(),compact?shared_data.data():bytes.data(),512} {
    std::memcpy(bytes.data(),compact?"cdex001":"dex\n035",7);
    auto u32=[&](size_t at,uint32_t value) { Write(bytes.data(),at,value); };
    const uint32_t table=compact?136:112;
    u32(32,bytes.size());u32(36,table);u32(40,0x12345678);
    u32(104,compact?512:312);u32(108,compact?0:200);
    u32(56,2);u32(60,table);u32(64,2);u32(68,table+8);u32(96,1);u32(100,table+16);
    u32(table,200);u32(table+4,300);u32(table+8,0);u32(table+12,1);u32(table+16+8,1);
    auto* data=const_cast<unsigned char*>(mapping.data_begin);
    data[200]=static_cast<unsigned char>(child.size());std::memcpy(data+201,child.c_str(),child.size()+1);
    data[300]=static_cast<unsigned char>(parent.size());std::memcpy(data+301,parent.c_str(),parent.size()+1);
    if(later) {
      assert(!compact);
      const auto original=bytes;
      constexpr size_t header=512;
      bytes.insert(bytes.end(),original.begin(),original.begin()+160);
      bytes.insert(bytes.begin()+header+112,8,0);
      std::memcpy(bytes.data()+header+4,"041",3);
      auto put=[&](size_t off,uint32_t value){Write(bytes.data(),header+off,value);};
      put(32,bytes.size()-header);put(36,120);put(104,0);put(108,0);
      put(112,bytes.size());put(116,header);
      put(60,header+120);put(68,header+128);put(100,header+136);
      mapping={nullptr,bytes.data()+header,bytes.size()-header,bytes.data(),bytes.size()};
    }
  }
};
size_t ClassNode(const char* name,size_t parent,size_t cache) {
  size_t id=New(1);auto* klass=static_cast<Class*>(Ptr(id));
  klass->class_loader=Ref(Ptr(loader_id));klass->super_class=parent==SIZE_MAX?0:Ref(Ptr(parent));
  klass->dex_cache=cache==SIZE_MAX?0:Ref(Ptr(cache));klass->status=15u<<28;
  names[name]=id;return id;
}
size_t CacheNode(Dex& dex) { size_t id=New(2);auto* cache=static_cast<Cache*>(Ptr(id));cache->prefix.dex_file=reinterpret_cast<uintptr_t>(&dex.mapping);return id; }
void* Find(void*,void*,const char* name,void* loader) {
  GC();assert(HierarchyRootPool::Get(loader)==Ptr(loader_id));
  auto found=names.find(name);
  if(found==names.end()) { Write<void*>(thread,handle_root_abi::kExceptionOffset,reinterpret_cast<void*>(123));return nullptr; }
  return Ptr(found->second);
}
void Fail(void*,int code,const void* signature,const BorrowedPlatformString* text) {
  assert(code==66);auto& own=*static_cast<const BorrowedPlatformString*>(signature);
  assert(std::strcmp(own.data,"NO CLASS")==0);failure=text->data;
}
void* Root(void* slot) { return reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(slot)}); }
void* Allocate(void*,void* old_slot,void* parent_slot,void* cache_slot,uint16_t index) {
  assert(index==0);GC();auto* old=static_cast<Class*>(HierarchyRootPool::Get(old_slot));
  auto view=ReadCopyClass(old,false);allocation_order.push_back(view.descriptor);
  size_t id=New(1);auto* fresh=static_cast<Class*>(Ptr(id));*fresh=*old;
  fresh->super_class=Ref(HierarchyRootPool::Get(parent_slot));fresh->dex_cache=Ref(HierarchyRootPool::Get(cache_slot));return fresh;
}
size_t oom_class;
std::vector<size_t> rejected_capture_classes;
unsigned oom_status=7;
void* AllocateThenOom(void* owner,void* old_slot,void* parent_slot,void* cache_slot,uint16_t index) {
  void* fresh=Allocate(owner,old_slot,parent_slot,cache_slot,index);
  oom_class=nodes.size()-1;
  static_cast<Class*>(fresh)->status=oom_status<<28;
  HandleRootScope roots(thread,5);roots.Set(0,fresh);
  CaptureHierarchyAllocatedClass(old_slot); // The published original is never a temporary.
  CaptureHierarchyAllocatedClass(roots.Slot(0));
  for(int kind=0;kind<3;++kind) {
    size_t id=New(1);rejected_capture_classes.push_back(id);
    auto* wrong=static_cast<Class*>(Ptr(id));*wrong=*static_cast<Class*>(roots.Get(0));
    if(kind==0) wrong->dex_cache=static_cast<Class*>(HierarchyRootPool::Get(old_slot))->dex_cache;
    if(kind==1) wrong->class_loader=0;
    if(kind==2) wrong->dex_type_index=1; // Same cache but descriptor names its superclass.
    roots.Set(kind+1,wrong);CaptureHierarchyAllocatedClass(roots.Slot(kind+1));
  }
  GC();assert(roots.Get(0)==Ptr(oom_class));
  // The named Ensure*Ids operation would now fail allocation. Capture must
  // retain the linked class even though AllocateNewClassObject returns null.
  return nullptr;
}
struct Hooks:HookEngine {
 bool Initialize(std::string*) override { return true; }
 void* FindSymbol(const char*,const char* symbol) override {
   if(std::strcmp(symbol,cas::kFindClass)==0) return reinterpret_cast<void*>(Find);
   if(std::strcmp(symbol,cas::kRecordFailure)==0) return reinterpret_cast<void*>(Fail);
   if(std::strcmp(symbol,"artReadBarrierForRootSlow")==0) return reinterpret_cast<void*>(Root);
   if(std::strstr(symbol,"HandleScopeVisitRoots"))return reinterpret_cast<void*>(HostHandleScopeVisitRoots);
   if(std::strstr(symbol,"GetPeerFromOtherThread"))return reinterpret_cast<void*>(Peer);
   if(!std::strcmp(symbol,"artJniMethodStart"))return reinterpret_cast<void*>(Native);
   if(!std::strcmp(symbol,"artJniMethodEnd"))return reinterpret_cast<void*>(Runnable);
   if(std::strcmp(symbol,handle_root_abi::kCurrentThread)==0) return reinterpret_cast<void*>(Current);
   if(!std::strcmp(symbol,"_ZN3art16ScopedSuspendAllC2EPKcb")) return reinterpret_cast<void*>(Suspend);
   if(!std::strcmp(symbol,"_ZN3art16ScopedSuspendAllD2Ev")) return reinterpret_cast<void*>(Resume);
   if(!std::strcmp(symbol,"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE")) return reinterpret_cast<void*>(Retire);
   return nullptr;
 }
 void* Install(void*,void*,void**,std::string*) override { return nullptr; }
 bool Remove(void*,std::string*) override { return true; }
};
struct Fixture {
  std::vector<redefinition_abi::ClassRedefinition> owners;
  hierarchy_allocation_abi::Vector vector{};
  uint64_t bits=0;
  allocation_abi::Holder holder{};
  HandleRootScope root{thread,1};
  Fixture(const std::vector<std::pair<size_t,size_t>>& entries,uint64_t structural):owners(entries.size()),bits(structural) {
    vector={reinterpret_cast<unsigned char*>(owners.data()),reinterpret_cast<unsigned char*>(owners.data()+owners.size()),reinterpret_cast<unsigned char*>(owners.data()+owners.capacity())};
    size_t id=New(4);Write<uint32_t>(Ptr(id),0,Ref(Ptr(array_type_id)));Write<int32_t>(Ptr(id),8,static_cast<int32_t>(entries.size()*13));
    for(size_t i=0;i<entries.size();++i) {
      Write<uint32_t>(Ptr(id),12+4*(13*i+3),Ref(Ptr(entries[i].second)));
      Write<uint32_t>(Ptr(id),12+4*(13*i+4),Ref(Ptr(entries[i].first)));
    }
    root.Set(0,Ptr(id));holder.array_handle=static_cast<uint32_t*>(root.Slot(0));holder.redefinitions=&vector;
    holder.initialized=holder.initial_structural=holder.actually_structural={&bits,entries.size(),1};
  }
};
}
namespace artdcevm::android13 {
void* RuntimeClassLinker() { return reinterpret_cast<void*>(1); }
void* ReadHeapReference(void* object,size_t offset,bool) { return reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(object,offset)}); }
bool IsValidationSubclassOf(void* source,void* target) {
  for(;source;source=ReadHeapReference(source,offsetof(Class,super_class),false)) if(source==target) return true;
  return false;
}
void CopyWriteBarrier(void*) { ++barriers; }
}
int main() {
  void* mapped=mmap(reinterpret_cast<void*>(0x20000000),64*1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
  assert(mapped!=MAP_FAILED && reinterpret_cast<uintptr_t>(mapped)+64*1024*1024<=UINT32_MAX);heap=static_cast<unsigned char*>(mapped);
  Hooks hooks;ConfigureTransactionReaderForTest(hooks);std::string error;
  loader_id=New(3);object_id=ClassNode("Ljava/lang/Object;",SIZE_MAX,SIZE_MAX);array_type_id=New(1);static_cast<Class*>(Ptr(array_type_id))->component_type=Ref(Ptr(object_id));
  assert(ConfigureHandleRootScopes(hooks,MatchedProfileForTest(),&error));assert(ConfigureHierarchyAllocation(hooks,MatchedProfileForTest(),&error));
  assert(ConfigureThreadSuspendScopes(hooks,MatchedProfileForTest(),&error));
  {
    HierarchyRootPool pool(thread);std::vector<void*> roots;
    for(int i=0;i<60;++i) roots.push_back(pool.Add(Ptr(object_id)));
    { HandleRootScope temporary(thread,1);temporary.Set(0,Ptr(loader_id));void* top=Read<void*>(thread,280);
      for(int i=0;i<150;++i) roots.push_back(pool.Add(Ptr(object_id)));
      assert(Read<void*>(thread,280)==top);GC();assert(temporary.Get(0)==Ptr(loader_id));
      for(auto slot:roots) assert(HierarchyRootPool::Get(slot)==Ptr(object_id));
    }
    GC();for(auto slot:roots) assert(HierarchyRootPool::Get(slot)==Ptr(object_id));
  }
  assert(Read<void*>(thread,280)==nullptr);
  Dex old_b("LB;","Ljava/lang/Object;"),old_c("LC;","Ljava/lang/Object;"),old_d("LD;","LB;");
  Dex new_b("LB;","Ljava/lang/Object;",false,true),new_c("LC;","LB;",true);
  Dex old_e("LE;","LC;");
  size_t old_bcache=CacheNode(old_b),old_ccache=CacheNode(old_c),old_dcache=CacheNode(old_d);
  b_id=ClassNode("LB;",object_id,old_bcache);c_id=ClassNode("LC;",object_id,old_ccache);d_id=ClassNode("LD;",b_id,old_dcache);
  b_cache_id=CacheNode(new_b);c_cache_id=CacheNode(new_c);
  {
    Fixture fixture({{c_id,c_cache_id},{b_id,b_cache_id}},3);
    HierarchyBatch batch(reinterpret_cast<void*>(10));assert(HasHierarchyAllocationContext());assert(batch.Prepare(&fixture.holder));
    HandleRootScope args(thread,3);args.Set(0,Ptr(c_id));args.Set(1,Ptr(c_cache_id));args.Set(2,Ptr(d_id));
    void* child=batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,Allocate);args.Set(0,child);
    assert((allocation_order==std::vector<std::string>{"LB;","LC;"}));
    void* parent=ReadHeapReference(args.Get(0),32,false);
    assert(parent==batch.Find("LB;",Ptr(loader_id)) && parent!=Ptr(b_id));
    args.Set(1,Ptr(old_dcache));void* descendant=batch.Allocate(&fixture.owners[1],args.Slot(2),args.Slot(1),0,Allocate);
    args.Set(2,descendant);assert(ReadHeapReference(descendant,32,false)==batch.Find("LB;",Ptr(loader_id)));
    args.Set(1,Ptr(c_cache_id));auto* cache=static_cast<Cache*>(args.Get(1));
    cache->types=reinterpret_cast<uintptr_t>(types);cache->num_types=4;cache->methods=reinterpret_cast<uintptr_t>(methods);cache->num_methods=4;
    cache->fields=reinterpret_cast<uintptr_t>(fields);cache->num_fields=3;
    types[0]={Ref(Ptr(b_id)),23};types[1]={0,89};for(auto& method:methods) method={123,321};for(auto& field:fields) field={444,555};
    { HierarchyVerificationView verify(args.Slot(0),args.Slot(1));
      assert(types[0].object==Ref(batch.Find("LB;",Ptr(loader_id))) && types[0].index==23);
      assert(types[1].object==0 && types[1].index==89);
      for(size_t i=0;i<4;++i) assert(methods[i].object==0 && methods[i].index==(i==0?1u:0u));
      for(size_t i=0;i<3;++i) assert(fields[i].object==0 && fields[i].index==(i==0?1u:0u));
      GC();assert(HierarchyVerificationView::Resolve("LC;",Ptr(loader_id),false)==args.Get(0));
      assert(HierarchyVerificationView::Resolve("LD;",Ptr(loader_id),false)==nullptr);
      assert(HierarchyVerificationView::Resolve("LD;",Ptr(loader_id),true)==args.Get(2));
      assert(HierarchyVerificationView::Resolve("LC;",nullptr,true)==nullptr);
      { HierarchyVerificationView nested(args.Slot(2),args.Slot(1));assert(HierarchyVerificationView::Resolve("LD;",Ptr(loader_id),false)==args.Get(2)); }
    }
    assert(HierarchyVerificationView::Resolve("LC;",Ptr(loader_id),true)==nullptr);
    // Implicit descendants retain the published cache: no native entries or
    // barrier may be changed before commit.
    auto* live=static_cast<Cache*>(Ptr(old_dcache));live->methods=reinterpret_cast<uintptr_t>(methods);live->num_methods=4;
    methods[0]={111,222};args.Set(1,Ptr(old_dcache));int before=barriers;
    { HierarchyVerificationView verify(args.Slot(2),args.Slot(1)); }
    assert(methods[0].object==111 && methods[0].index==222 && barriers==before);
  }
  assert(!HasHierarchyAllocationContext() && Read<void*>(thread,280)==nullptr);
  {
    // Obsolete and current generations share descriptor+loader. Lookup falls
    // back to the most recent allocation, while verifier ancestry keeps the
    // exact generation already selected by ART.
    size_t obsolete=New(1);*static_cast<Class*>(Ptr(obsolete))=*static_cast<Class*>(Ptr(b_id));
    Fixture fixture({{b_id,b_cache_id}},1);HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));
    HandleRootScope args(thread,4);args.Set(0,Ptr(obsolete));args.Set(1,Ptr(b_cache_id));args.Set(2,Ptr(b_id));
    args.Set(0,batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,Allocate));
    args.Set(2,batch.Allocate(&fixture.owners[0],args.Slot(2),args.Slot(1),0,Allocate));
    assert(args.Get(0)!=args.Get(2) && batch.Find("LB;",Ptr(loader_id))==args.Get(2));
    { HierarchyVerificationView verify(args.Slot(0),args.Slot(1));GC();
      assert(HierarchyVerificationView::Resolve("LB;",Ptr(loader_id),false)==args.Get(0));
      assert(HierarchyVerificationView::Resolve("LB;",Ptr(loader_id),true)==args.Get(0));
      { HierarchyVerificationView nested(args.Slot(2),args.Slot(1));
        assert(HierarchyVerificationView::Resolve("LB;",Ptr(loader_id),true)==args.Get(2)); }
      assert(HierarchyVerificationView::Resolve("LB;",Ptr(loader_id),true)==args.Get(0));
    }
    // Original allocator arguments are preserved when the requested parent
    // already matches, including a different owner with the same descriptor.
    args.Set(3,Ptr(object_id));
    args.Set(0,AllocateHierarchyWithoutBatch(&fixture.owners[0],args.Slot(2),args.Slot(3),args.Slot(1),0,Allocate));
    assert(ReadHeapReference(args.Get(0),32,false)==Ptr(object_id));
  }
  {
    // A verifier-failed temporary class remains allocated until GC. ART's next
    // heap subtype scan must skip it before reading the now-destroyed DexFile.
    // Include an implicit descendant and relocate every rooted class before
    // completion; retirement must follow handles, not stale native addresses.
    Dex failed_dex("LC;","LB;");
    size_t failed_cache=CacheNode(failed_dex),ecache=CacheNode(old_e);
    size_t e_id=ClassNode("LE;",c_id,ecache);
    size_t failed_class,failed_descendant;
    int before=retire_calls;
    {
      Fixture fixture({{c_id,failed_cache}},1);
      HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));
      HandleRootScope args(thread,4);args.Set(0,Ptr(c_id));args.Set(1,Ptr(failed_cache));
      failed_class=nodes.size();
      args.Set(0,batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,Allocate));
      assert(args.Get(0)==Ptr(failed_class));
      assert(IsValidationSubclassOf(args.Get(0),Ptr(b_id)));
      args.Set(2,Ptr(e_id));args.Set(3,Ptr(ecache));failed_descendant=nodes.size();
      args.Set(2,batch.Allocate(&fixture.owners[0],args.Slot(2),args.Slot(3),0,Allocate));
      assert(args.Get(2)==Ptr(failed_descendant));
      // An uninitialized old descendant preserves a resolved status in ART.
      static_cast<Class*>(args.Get(2))->status=7u<<28;
      GC();assert(args.Get(0)==Ptr(failed_class) && args.Get(2)==Ptr(failed_descendant));
      batch.Complete(false);
      assert(retire_calls==before+2 && !exclusive_mutator && Read<uint32_t>(thread)==0);
      assert(static_cast<Class*>(Ptr(failed_class))->status==(1u<<28));
      assert(static_cast<Class*>(Ptr(failed_descendant))->status==(1u<<28));
      batch.Complete(false);assert(retire_calls==before+2); // Completion is idempotent.
    }
    // Model allocator reuse: old temporary cache now names a different DEX.
    static_cast<Cache*>(Ptr(failed_cache))->prefix.dex_file=reinterpret_cast<uintptr_t>(&new_b.mapping);
    failed_dex.mapping={};
    GC();
    int scanned=0;
    for(const auto& node:nodes) {
      if(node.kind!=1) continue;
      unsigned status=static_cast<Class*>(node.pointer)->status>>28;
      if(!(status>=7 || status==2) || !IsValidationSubclassOf(node.pointer,Ptr(b_id))) continue;
      ++scanned;
      assert(node.pointer!=Ptr(failed_class) && node.pointer!=Ptr(failed_descendant));
      auto* cache=static_cast<Cache*>(ReadHeapReference(node.pointer,offsetof(Class,dex_cache),false));
      assert(cache && cache!=Ptr(failed_cache));
    }
    assert(scanned>0);
    // Successful publication owns the native DEX; no class may be retired.
    Fixture fixture({{c_id,c_cache_id}},1);
    HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));
    HandleRootScope args(thread,2);args.Set(0,Ptr(c_id));args.Set(1,Ptr(c_cache_id));
    args.Set(0,batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,Allocate));
    batch.Complete(true);batch.Complete(true);batch.Complete(false);
    assert(retire_calls==before+2 && (static_cast<Class*>(args.Get(0))->status>>28)==15);
  }
  {
    for(unsigned status:{7u,15u}) {
      Dex failed_dex("LC;","LB;");size_t cache=CacheNode(failed_dex);
      Fixture fixture({{c_id,cache}},1);
      HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));
      HandleRootScope args(thread,3);args.Set(0,Ptr(c_id));args.Set(1,Ptr(cache));
      oom_status=status;rejected_capture_classes.clear();int before=retire_calls;
      assert(batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,AllocateThenOom)==nullptr);
      assert(batch.Find("LC;",Ptr(loader_id))==Ptr(oom_class));
      // Ensure calls on an ordinary class after the allocator's TLS scope
      // ends cannot overwrite the captured failed allocation, even if its
      // descriptor, loader and cache happen to match exactly.
      size_t ordinary=New(1);*static_cast<Class*>(Ptr(ordinary))=*static_cast<Class*>(Ptr(oom_class));
      args.Set(2,Ptr(ordinary));CaptureHierarchyAllocatedClass(args.Slot(2));
      CaptureHierarchyAllocatedClass(nullptr);GC();
      batch.Complete(false);
      assert(retire_calls==before+1 && static_cast<Class*>(Ptr(oom_class))->status==(1u<<28));
      assert((static_cast<Class*>(args.Get(2))->status>>28)==status);
      for(size_t id:rejected_capture_classes) assert((static_cast<Class*>(Ptr(id))->status>>28)==status);
      assert((static_cast<Class*>(Ptr(c_id))->status>>28)==15);
    }
  }
  {
    Dex cyclic_b("LB;","LC;"),cyclic_c("LC;","LB;");size_t bc=CacheNode(cyclic_b),cc=CacheNode(cyclic_c);
    Fixture fixture({{b_id,bc},{c_id,cc}},3);HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));
    HandleRootScope args(thread,2);args.Set(0,Ptr(c_id));args.Set(1,Ptr(cc));
    assert(batch.Allocate(&fixture.owners[1],args.Slot(0),args.Slot(1),0,Allocate)==nullptr);
    assert(failure=="Cycle in the requested batch superclass graph");
  }
  {
    Dex missing("LC;","LMissing;");size_t cc=CacheNode(missing);Fixture fixture({{c_id,cc}},1);
    HierarchyBatch batch(reinterpret_cast<void*>(10));assert(batch.Prepare(&fixture.holder));HandleRootScope args(thread,2);
    args.Set(0,Ptr(c_id));args.Set(1,Ptr(cc));assert(batch.Allocate(&fixture.owners[0],args.Slot(0),args.Slot(1),0,Allocate)==nullptr);
    assert(failure=="Cannot resolve a superclass during batch allocation" && Read<void*>(thread,152)==nullptr);
  }
  assert(Read<void*>(thread,280)==nullptr && gcs>=10 && HierarchyDiagnostics(3)>=2 && HierarchyDiagnostics(4)>=1);
  munmap(heap,64*1024*1024);
  assert(suspend_calls>0 && !exclusive_mutator);
  std::cout<<"hierarchy allocation: moving GC, anchor growth, recursive dependencies/cycles, verifier priority, failed-batch retirement before DexFile destruction, linked-class OOM capture/rejection and unpublished cache boundary passed\n";
}

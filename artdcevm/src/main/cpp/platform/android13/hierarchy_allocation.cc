#include "platform/android13/hierarchy_allocation.h"
#include "platform/android13/hierarchy_allocation_abi.h"
#include "platform/android13/handle_root_scope_abi.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_storage.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/platform_string_abi.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/runtime_access.h"
#include "platform/symbols/android13.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "platform/android13/transaction_metadata.h"
#include <limits>
namespace artdcevm::android13 {
namespace {
using Class=validation_abi::ClassPrefix;
using Cache=hierarchy_allocation_abi::Cache;
using Find=void*(*)(void*,void*,const char*,void*);
using Failure=void(*)(void*,int,const void*,const BorrowedPlatformString*);
Find find_class=nullptr;
Failure record_failure=nullptr;
void*(*read_root)(void*)=nullptr;
thread_local HierarchyBatch* batch=nullptr;
thread_local HierarchyVerificationView* view=nullptr;
std::atomic<uint64_t> counters[5]{};
void Require(bool value) { if(!value) std::abort(); }
template<class T> T Read(const void* object,size_t at=0) {
  T value;std::memcpy(&value,static_cast<const unsigned char*>(object)+at,sizeof(value));return value;
}
template<class T> void Write(void* object,size_t at,T value) {
  std::memcpy(static_cast<unsigned char*>(object)+at,&value,sizeof(value));
}
void* Parent(void* klass) { return ReadHeapReference(klass,offsetof(Class,super_class)); }
void* Loader(void* klass) { return ReadHeapReference(klass,offsetof(Class,class_loader)); }
bool Normal(void* klass) {
  return (Read<uint32_t>(klass,offsetof(Class,primitive_type))&0xffff)==0 &&
      !ReadHeapReference(klass,offsetof(Class,component_type)) &&
      !(Read<uint32_t>(klass,offsetof(Class,access_flags))&0x40000); // kAccClassIsProxy
}
const char* Descriptor(void* klass) { Require(Normal(klass));return ReadCopyClass(klass,false).descriptor; }
bool Matches(void* klass,const char* descriptor,void* loader) {
  // Arrays, primitives and generated proxies cannot be modifiable batch targets.
  return Normal(klass) && Loader(klass)==loader && std::strcmp(Descriptor(klass),descriptor)==0;
}
struct Dex {
  validation_abi::DexMappingPrefix mapping;
  void Range(size_t at,size_t width,size_t size) const { Require(at<=size && width<=size-at); }
  uint32_t U32(const unsigned char* base,size_t size,size_t at) const {
    Range(at,4,size);return uint32_t{base[at]}|uint32_t{base[at+1]}<<8|uint32_t{base[at+2]}<<16|uint32_t{base[at+3]}<<24;
  }
  size_t Id(size_t header,uint32_t index,size_t width) const {
    uint32_t count=U32(mapping.begin,mapping.size,header),at=U32(mapping.begin,mapping.size,header+4);
    Require(index<count && at>=0x70 && at%4==0);Range(at,size_t{count}*width,mapping.size);
    return at+size_t{index}*width;
  }
  const char* ParentDescriptor(uint16_t index) const {
    size_t def=Id(96,index,32);Range(def+8,2,mapping.size);
    const uint16_t type=uint16_t(mapping.begin[def+8]|uint16_t{mapping.begin[def+9]}<<8);
    uint32_t string=U32(mapping.begin,mapping.size,Id(64,type,4));
    size_t at=U32(mapping.begin,mapping.size,Id(56,string,4));bool complete=false;
    for(unsigned i=0;i<5;++i) {
      Range(at,1,mapping.data_size);unsigned byte=mapping.data_begin[at++];
      if(i==4) Require((byte&0xf0)==0);
      if(!(byte&0x80)) { complete=true;break; }
    }
    Require(complete);Range(at,1,mapping.data_size);
    Require(std::memchr(mapping.data_begin+at,0,mapping.data_size-at));
    return reinterpret_cast<const char*>(mapping.data_begin+at);
  }
};
const char* Requested(void* cache,uint16_t index) {
  auto* native=reinterpret_cast<void*>(Read<uint64_t>(cache,offsetof(Cache,prefix)+offsetof(validation_abi::DexCachePrefix,dex_file)));
  Require(native);auto mapping=Read<validation_abi::DexMappingPrefix>(native);
  Require(mapping.begin && mapping.data_begin);return Dex{mapping}.ParentDescriptor(index);
}
void ClearNative(uint64_t address,uint32_t count) {
  using Pair=hierarchy_allocation_abi::NativePair;
#if defined(__aarch64__)
  static_assert(__atomic_always_lock_free(sizeof(Pair),nullptr));
#endif
  auto* pairs=reinterpret_cast<Pair*>(address);
  for(uint32_t i=0;pairs && i<count;++i) {
    Pair empty{0,i==0?uintptr_t{1}:uintptr_t{0}};
    __atomic_store(pairs+i,&empty,__ATOMIC_RELEASE);
  }
}
}
HierarchyRootPool::HierarchyRootPool(void* thread):anchor_(thread,0),original_link_(Read<void*>(anchor_.NativeScope())) {}
HierarchyRootPool::~HierarchyRootPool() {
  // Restore the original chain while every chunk still exists. The anchor's
  // destructor subsequently pops normally, after all device scopes unwound.
  Write(anchor_.NativeScope(),0,original_link_);
}
void* HierarchyRootPool::Add(void* object) {
  if(used_==64) {
    auto storage=std::make_unique<unsigned char[]>(handle_root_abi::kReferencesOffset+64*4);
    handle_root_abi::ScopeHeader header{Read<void*>(anchor_.NativeScope()),64};
    std::memcpy(storage.get(),&header,sizeof(header));
    chunks_.push_back(std::move(storage)); // finish native allocation before publication
    Write<void*>(anchor_.NativeScope(),0,chunks_.back().get());used_=0;
  }
  void* slot=chunks_.back().get()+handle_root_abi::kReferencesOffset+4*used_++;
  Set(slot,object);return slot;
}
void* HierarchyRootPool::Get(const void* slot) { return slot?reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(slot)}):nullptr; }
void HierarchyRootPool::Set(void* slot,void* object) {
  Require(slot && reinterpret_cast<uintptr_t>(object)<=UINT32_MAX);
  Write(slot,0,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)));
}
HierarchyBatch::HierarchyBatch(void* driver):driver_(driver),roots_(CurrentHierarchyThread()),previous_(batch) { batch=this; }
HierarchyBatch::~HierarchyBatch() { Require(batch==this);batch=previous_; }
HierarchyBatch* HierarchyBatch::Current() { return batch; }
bool HasHierarchyAllocationContext() { return batch!=nullptr; }
void HierarchyBatch::Fail(const char* message) {
  BorrowedPlatformString signature("NO CLASS"),text(message);
  record_failure(driver_,66 /* UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED */,&signature,&text);
}
bool HierarchyBatch::Prepare(void* raw) {
  if(!targets_.empty()) return true;
  if(!raw) { Fail("Missing batch allocation data");return false; }
  TransactionHolderView holder;std::string error;
  if(!GetTransactionMetadataReader().ReadHolder(raw,&holder,&error)) { Fail("Invalid batch allocation transaction layout");return false; }
  size_t count=holder.count;
  targets_.reserve(count);
  for(size_t i=0;i<count;++i) {
    // No suspension occurs during metadata reads or native root publication.
    allocation_abi::Iterator iterator{static_cast<int32_t>(i),static_cast<allocation_abi::Holder*>(raw)};TransactionRootView roots;
    if(!GetTransactionMetadataReader().ReadRoots(&iterator,&roots,&error)||!roots.mirror||!roots.cache) {
      Fail("Invalid batch allocation target roots");return false;
    }
    hierarchy_allocation_abi::Target snapshot{roots.owner,roots.mirror,roots.cache,roots.actually_structural};
    bool structural=roots.actually_structural;
    targets_.push_back({snapshot.owner,roots_.Add(snapshot.klass),roots_.Add(snapshot.cache),structural});
  }
  return true;
}
void* HierarchyBatch::Allocate(void* owner,void* old_slot,void* cache_slot,uint16_t index,HierarchyClassAllocator original) {
  if(targets_.empty()) { Fail("The subtree allocation hook did not prepare the batch");return nullptr; }
  for(const auto& entry:replacements_) if(HierarchyRootPool::Get(entry.old_class)==HierarchyRootPool::Get(old_slot)) {
    auto replacement=HierarchyRootPool::Get(entry.new_class);
    if(!replacement) Fail("Cycle in the requested batch superclass graph");return replacement;
  }
  size_t slot=replacements_.size();
  replacements_.push_back({roots_.Add(HierarchyRootPool::Get(old_slot)),roots_.Add(nullptr)});
  // Requested points into immutable native DEX, so it survives GC below.
  const char* requested=Requested(HierarchyRootPool::Get(cache_slot),index);
  void* loader=roots_.Add(Loader(HierarchyRootPool::Get(old_slot)));
  void* parent=roots_.Add(find_class(RuntimeClassLinker(),CurrentHierarchyThread(),requested,loader));
  if(!HierarchyRootPool::Get(parent)) {
    ClearHierarchyException(CurrentHierarchyThread());Fail("Cannot resolve a superclass during batch allocation");return nullptr;
  }
  size_t parent_target=targets_.size(),structural_root=targets_.size();
  for(size_t i=0;i<targets_.size();++i) {
    if(HierarchyRootPool::Get(targets_[i].klass)==HierarchyRootPool::Get(parent)) parent_target=i;
    if(targets_[i].structural && IsValidationSubclassOf(HierarchyRootPool::Get(parent),HierarchyRootPool::Get(targets_[i].klass))) structural_root=i;
  }
  void* selected=roots_.Add(HierarchyRootPool::Get(parent));
  if(structural_root<targets_.size()) {
    ++counters[3];
    const bool explicit_parent=parent_target<targets_.size();
    void* parent_cache=explicit_parent?targets_[parent_target].cache:
        roots_.Add(ReadHeapReference(HierarchyRootPool::Get(parent),offsetof(Class,dex_cache)));
    auto parent_index=explicit_parent?uint16_t{0}:static_cast<uint16_t>(Read<int32_t>(HierarchyRootPool::Get(parent),offsetof(Class,dex_class_def_index)));
    void* parent_owner=targets_[explicit_parent?parent_target:structural_root].owner;
    HierarchyRootPool::Set(selected,Allocate(parent_owner,parent,parent_cache,parent_index,original));
    if(!HierarchyRootPool::Get(selected)) return nullptr;
  }
  if(std::strcmp(Descriptor(Parent(HierarchyRootPool::Get(old_slot))),requested)!=0) ++counters[0];
  void* result=original(owner,old_slot,selected,cache_slot,index);
  // Recursive allocation can reallocate the native vector; use the saved index.
  HierarchyRootPool::Set(replacements_[slot].new_class,result);return result;
}
void* HierarchyBatch::Find(const char* name,void* loader) {
  for(auto it=replacements_.rbegin();it!=replacements_.rend();++it) {
    void* klass=HierarchyRootPool::Get(it->new_class);
    if(klass && Matches(klass,name,loader)) { ++counters[1];return klass; }
  }
  return nullptr;
}
void HierarchyBatch::RefreshCache(void* slot) {
  void* cache=HierarchyRootPool::Get(slot);bool owned=false;
  for(const auto& target:targets_) owned|=HierarchyRootPool::Get(target.cache)==cache;
  if(!owned) return; // Only unpublished explicit-target caches may be mutated.
  Cache snapshot;std::string error;Require(GetTransactionMetadataReader().ReadCache(cache,&snapshot,&error));
  using Pair=hierarchy_allocation_abi::TypePair;
  auto types=reinterpret_cast<Pair*>(snapshot.types);
  uint32_t count=snapshot.num_types;
  for(uint32_t i=0;types && i<count;++i) {
    Pair pair;__atomic_load(types+i,&pair,__ATOMIC_RELAXED);
    void* klass=read_root(&pair.object);
    if(!klass || !Normal(klass)) continue;
    void* replacement=Find(Descriptor(klass),Loader(klass));
    if(replacement && replacement!=klass) {
      pair.object=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement));
      __atomic_store(types+i,&pair,__ATOMIC_RELEASE);++counters[4];
    }
  }
  ClearNative(snapshot.methods,snapshot.num_methods);
  ClearNative(snapshot.fields,snapshot.num_fields);
  CopyWriteBarrier(cache);
}
void* AllocateHierarchyWithoutBatch(void* owner,void* old_slot,void* parent_slot,void* cache_slot,uint16_t index,HierarchyClassAllocator original) {
  const char* requested=Requested(HierarchyRootPool::Get(cache_slot),index);
  if(std::strcmp(Descriptor(HierarchyRootPool::Get(parent_slot)),requested)==0)
    return original(owner,old_slot,parent_slot,cache_slot,index);
  HandleRootScope roots(CurrentHierarchyThread(),2);
  roots.Set(0,Loader(HierarchyRootPool::Get(old_slot)));
  roots.Set(1,find_class(RuntimeClassLinker(),CurrentHierarchyThread(),requested,roots.Slot(0)));
  Require(roots.Get(1)!=nullptr);
  Require((__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(roots.Get(1))+offsetof(Class,status)),__ATOMIC_SEQ_CST)>>28)>=14);
  ++counters[0];return original(owner,old_slot,roots.Slot(1),cache_slot,index);
}
HierarchyVerificationView::HierarchyVerificationView(void* klass,void* cache):class_slot_(HierarchyRootPool::Get(klass)?klass:nullptr),previous_(view) {
  if(!class_slot_) return;
  view=this;++counters[2];if(batch) batch->RefreshCache(cache);
}
HierarchyVerificationView::~HierarchyVerificationView() { if(class_slot_) { Require(view==this);view=previous_; } }
void* HierarchyVerificationView::Resolve(const char* descriptor,void* loader,bool allow_batch) {
  for(auto* cursor=view;cursor;cursor=cursor->previous_)
    for(void* klass=HierarchyRootPool::Get(cursor->class_slot_);klass;klass=Parent(klass))
      if(Matches(klass,descriptor,loader)) { ++counters[1];return klass; }
  return allow_batch && view && batch?batch->Find(descriptor,loader):nullptr;
}
uint64_t HierarchyDiagnostics(int index) { return counters[index>=0 && index<4?index:4].load(); }
bool ConfigureHierarchyAllocation(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()){*error="Hierarchy allocation requires a matched device profile";return false;}
  auto find=reinterpret_cast<Find>(hooks.FindSymbol("libart.so",cas::kFindClass));
  auto failure=reinterpret_cast<Failure>(hooks.FindSymbol("libopenjdkjvmti.so",cas::kRecordFailure));
  auto root=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  if(!find || !failure || !root || !RuntimeClassLinker()) {
    *error="Missing named hierarchy allocation functions or ClassLinker";return false;
  }
  find_class=find;record_failure=failure;read_root=root;return true;
}
}

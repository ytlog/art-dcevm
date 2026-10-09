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
#include "platform/common/dex_file_metadata.h"
#include "platform/common/native_cache_reset.h"
#include "platform/common/type_cache_update.h"
#include <limits>
#include "platform/android13/thread_suspend_scope.h"
namespace artdcevm::android13 {
namespace {
using Class=validation_abi::ClassPrefix;
using Cache=hierarchy_allocation_abi::Cache;
using Find=void*(*)(void*,void*,const char*,void*);
using Failure=void(*)(void*,int,const void*,const BorrowedPlatformString*);
Find find_class=nullptr;
Failure record_failure=nullptr;
void (*set_status_locked)(void*,uint8_t)=nullptr;
struct AllocationCapture { void* result_slot;void* old_slot;void* cache_slot; };
thread_local AllocationCapture* allocation_capture=nullptr;
void*(*read_root)(void*)=nullptr;
thread_local HierarchyBatch* batch=nullptr;
thread_local HierarchyVerificationView* view=nullptr;
std::atomic<uint64_t> counters[7]{};
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
  class_metadata::State state;
  Require(class_metadata::ReadState(class_metadata::kLegacy,klass,sizeof(Class),&state));
  return !state.IsPrimitive() &&
      !ReadHeapReference(klass,offsetof(Class,component_type)) &&
      !(state.access_flags&0x40000); // kAccClassIsProxy
}
const char* Descriptor(void* klass) { Require(Normal(klass));return ReadCopyClass(klass,false).descriptor; }
bool Matches(void* klass,const char* descriptor,void* loader) {
  // Arrays, primitives and generated proxies cannot be modifiable batch targets.
  return Normal(klass) && Loader(klass)==loader && std::strcmp(Descriptor(klass),descriptor)==0;
}
const char* Requested(void* cache,uint16_t index) {
  evolution::MappedDexMetadata mapping;
  Require(dex_file_metadata::ReadCache(33,cache,sizeof(Cache),sizeof(validation_abi::DexMappingPrefix),
                                      index,&mapping,nullptr));
  const char* parent = nullptr;
  std::string error;
  Require(evolution::ReadMappedDexSuperclassDescriptor(mapping,&parent,&error) && parent);
  return parent;
}
void ClearNative(uint64_t address,uint32_t count) {
  Require(native_cache::ResetPairs(33,reinterpret_cast<void*>(static_cast<uintptr_t>(address)),count));
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
  std::vector<rooted_hierarchy::Target> prepared;
  for(const auto& target:targets_)prepared.push_back({target.owner,target.klass,target.cache,target.structural});
  if(!allocator_.Prepare(std::move(prepared))){Fail("Invalid or repeated rooted hierarchy allocation graph");return false;}
  return true;
}
void* HierarchyBatch::Allocate(void* owner,void* old_slot,void* cache_slot,uint16_t index,HierarchyClassAllocator original) {
  struct Adapter final:rooted_hierarchy::Access {
    HierarchyBatch& batch;HierarchyClassAllocator original;
    Adapter(HierarchyBatch& b,HierarchyClassAllocator f):batch(b),original(f){}
    void* Copy(void* slot)override{return batch.roots_.Add(HierarchyRootPool::Get(slot));}
    void* Empty()override{return batch.roots_.Add(nullptr);}
    void* Get(void* slot)const override{return HierarchyRootPool::Get(slot);}
    void Set(void* slot,void* object)override{HierarchyRootPool::Set(slot,object);}
    void* Loader(void* slot)override{return batch.roots_.Add(android13::Loader(Get(slot)));}
    void* Cache(void* slot)override{return batch.roots_.Add(ReadHeapReference(Get(slot),offsetof(Class,dex_cache)));}
    uint16_t ClassIndex(void* slot)const override{return static_cast<uint16_t>(Read<int32_t>(Get(slot),offsetof(Class,dex_class_def_index)));}
    std::string RequestedParent(void* cache,uint16_t index)override{return Requested(Get(cache),index);}
    void* Resolve(const std::string& name,void* loader)override{
      void* found=find_class(RuntimeClassLinker(),CurrentHierarchyThread(),name.c_str(),loader);
      if(!found)ClearHierarchyException(CurrentHierarchyThread());
      return batch.roots_.Add(found);
    }
    bool Subclass(void* child,void* parent)const override{
      const bool value=IsValidationSubclassOf(Get(child),Get(parent));if(value)++counters[3];return value;
    }
    void* Allocate(void* owner,void* old,void* parent,void* cache,uint16_t index,void* result)override{
      if(std::strcmp(Descriptor(Parent(Get(old))),Requested(Get(cache),index))!=0)++counters[0];
      AllocationCapture capture{result,old,cache};
      struct Restore {AllocationCapture* previous;~Restore(){allocation_capture=previous;}} restore{allocation_capture};
      allocation_capture=&capture;return original(owner,old,parent,cache,index);
    }
    bool ValidateReplacement(void* old,void* result,void* parent,void* cache)const override{
      void* klass=Get(result);return klass&&klass!=Get(old)&&
        Matches(klass,Descriptor(Get(old)),android13::Loader(Get(old)))&&
        Parent(klass)==Get(parent)&&ReadHeapReference(klass,offsetof(Class,dex_cache))==Get(cache);
    }
    void Fail(const char* message)override{batch.Fail(message);}
  } adapter(*this,original);
  return allocator_.Allocate(adapter,owner,old_slot,cache_slot,index);
}
void CaptureHierarchyAllocatedClass(const void* class_slot) {
  if(!allocation_capture || !class_slot)return;
  auto& capture=*allocation_capture;
  void* klass=HierarchyRootPool::Get(class_slot);
  void* old=HierarchyRootPool::Get(capture.old_slot);
  if(!klass || klass==old ||
     ReadHeapReference(klass,offsetof(Class,dex_cache))!=HierarchyRootPool::Get(capture.cache_slot) ||
     !Matches(klass,Descriptor(old),Loader(old)))return;
  void* slot=capture.result_slot;
  if(HierarchyRootPool::Get(slot)!=klass)++counters[6];
  HierarchyRootPool::Set(slot,klass);
}
void HierarchyBatch::Complete(bool committed) {
  if(!committed && !allocator_.Replacements().empty()) {
    void* self=CurrentHierarchyThread();
    NativeSuspendScope native(self);Require(native.Supported());
    OwnedSuspendAll suspended("Retire failed hierarchy allocations");
    for(const auto& replacement:allocator_.Replacements()) {
      void* klass=HierarchyRootPool::Get(replacement.new_class);
      if(!klass)continue;
      Require(klass!=HierarchyRootPool::Get(replacement.old_class));
      const uint32_t status=Read<uint32_t>(klass,offsetof(Class,status))>>28;
      // Fixed IsResolved includes ErrorResolved(2). ForceClassInitialized waits
      // for visibility before the Ensure*Ids capture/normal allocation return.
      if(status>=7 || status==2) {
        Require(status!=14);
        set_status_locked(klass,1); // Fixed ClassStatus::kRetired, not an error.
        Require((Read<uint32_t>(klass,offsetof(Class,status))>>28)==1);
        ++counters[5];
      }
    }
  }
  allocator_.Clear(); // Successful publication can never be retired later.
}
void* HierarchyBatch::Find(const char* name,void* loader) {
  for(auto it=allocator_.Replacements().rbegin();it!=allocator_.Replacements().rend();++it) {
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
  type_cache::View types;
  Require(type_cache::Read(dex_cache_metadata::Model::Pairs,cache,sizeof(Cache),0,&types));
  types.Apply([&](uint32_t* reference) {
    void* klass=read_root(reference);
    if(!klass || !Normal(klass))return false;
    void* replacement=Find(Descriptor(klass),Loader(klass));
    if(!replacement || replacement==klass)return false;
    *reference=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement));
    ++counters[4];return true;
  });
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
uint64_t HierarchyDiagnostics(int index) { return counters[index>=0 && index<7?index:4].load(); }
bool ConfigureHierarchyAllocation(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()){*error="Hierarchy allocation requires a matched device profile";return false;}
  auto find=reinterpret_cast<Find>(hooks.FindSymbol("libart.so",cas::kFindClass));
  auto failure=reinterpret_cast<Failure>(hooks.FindSymbol("libopenjdkjvmti.so",cas::kRecordFailure));
  auto root=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto retire=reinterpret_cast<decltype(set_status_locked)>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE"));
  if(!find || !failure || !root || !retire || !RuntimeClassLinker()) {
    *error="Missing named hierarchy allocation functions or ClassLinker";return false;
  }
  find_class=find;record_failure=failure;read_root=root;set_status_locked=retire;return true;
}
}

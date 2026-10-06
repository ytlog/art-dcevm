#include "hierarchy.h"
#include "class_linker-inl.h"
#include "dex/dex_file-inl.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/iftable-inl.h"
#include "runtime.h"
#include "thread-current-inl.h"
#include "verifier/class_verifier.h"
#include <atomic>
#include <vector>
#include <android/log.h>

namespace artdcevm::android13::source {
namespace {
thread_local bool hierarchy_changed = false;
thread_local bool hierarchy_narrowed = false;
}
extern "C" bool ArtDcevmSourceReceiverGuardEnabled();
HierarchyTransactionScope::HierarchyTransactionScope()
    : previous_(hierarchy_changed), previous_narrowed_(hierarchy_narrowed) {
  hierarchy_changed = false;
  hierarchy_narrowed = false;
}
HierarchyTransactionScope::~HierarchyTransactionScope() {
  hierarchy_changed = previous_;
  hierarchy_narrowed = previous_narrowed_;
}
bool HasHierarchyChange() { return hierarchy_changed; }
bool HasHierarchyNarrowing() { return hierarchy_narrowed; }
jvmtiError CheckHierarchyAdditions(art::Handle<art::mirror::Class> current,
    const art::DexFile& dex, art::MutableHandle<art::mirror::Class>* desired_super,
    bool* changed, std::string* error) {
  auto* self = art::Thread::Current();
  auto* linker = art::Runtime::Current()->GetClassLinker();
  art::VariableSizedHandleScope hs(self);
  auto loader = hs.NewHandle(current->GetClassLoader());
  auto fail = [&](const char* message) {
    if (self->IsExceptionPending()) self->ClearException();
    *error = message;
    return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  };
  const auto& def = dex.GetClassDef(0);
  if (current->IsObjectClass()) return fail("The Object root cannot evolve its hierarchy");
  if (!def.superclass_idx_.IsValid()) return fail("Missing superclass");
  const char* name = dex.StringByTypeIdx(def.superclass_idx_);
  *changed = !current->GetSuperClass()->DescriptorEquals(name);
  desired_super->Assign(linker->FindClass(self, name, loader));
  if (desired_super->IsNull()) return fail("Cannot resolve requested superclass");
  auto parent = desired_super->Get();
  if (parent->IsInterface() || parent->IsFinal() || !current->CanAccess(parent))
    return fail("Invalid or inaccessible superclass");
  if (parent->IsSubClass(current.Get())) return fail("Cyclic superclass change");
  bool loses_parent = !parent->IsSubClass(current->GetSuperClass());
  if (loses_parent && !ArtDcevmSourceReceiverGuardEnabled())
    return fail("Removing a superclass requires the active receiver guard");
  hierarchy_narrowed |= loses_parent;
  if (*changed && !parent->IsInitialized())
    return fail("Initialize the added superclass before applying this evolution");
  // Resolve every direct interface before ART pauses class definition. Keep
  // handles across FindClass calls, which may allocate and run a GC.
  std::vector<art::Handle<art::mirror::Class>> interfaces;
  const auto* types = dex.GetInterfacesList(def);
  if (types) {
    for (uint32_t i = 0; i < types->Size(); ++i) {
      auto iface = hs.NewHandle(linker->FindClass(self,
          dex.StringByTypeIdx(types->GetTypeItem(i).type_idx_), loader));
      if (iface.IsNull() || !iface->IsInterface() || !current->CanAccess(iface.Get()))
        return fail("Invalid or inaccessible interface");
      interfaces.push_back(iface);
    }
  }
  // Retain assignability, including old transitive interfaces and interfaces
  // now inherited through the added parent or through a new subinterface.
  auto old_table = current->GetIfTable();
  for (int32_t i = 0; i < old_table->Count(); ++i) {
    auto old_interface = old_table->GetInterface(i);
    bool retained = old_interface->IsAssignableFrom(desired_super->Get());
    for (const auto& iface : interfaces) retained |= old_interface->IsAssignableFrom(iface.Get());
    if (!retained && !ArtDcevmSourceReceiverGuardEnabled())
      return fail("Removing an interface requires the active receiver guard");
    hierarchy_narrowed |= !retained;
  }
  const auto* old_types = current->GetInterfaceTypeList();
  uint32_t old_count = old_types ? old_types->Size() : 0;
  uint32_t new_count = types ? types->Size() : 0;
  *changed |= old_count != new_count;
  for (uint32_t i = 0; i < old_count && i < new_count; ++i) {
    const char* old_name = current->GetDexFile().StringByTypeIdx(old_types->GetTypeItem(i).type_idx_);
    *changed |= !interfaces[i]->DescriptorEquals(old_name);
  }
  hierarchy_changed |= *changed;
  return JVMTI_ERROR_NONE;
}
}

namespace {
std::atomic<uint64_t> parent_selections{0}, verifier_resolutions{0}, verifier_entries{0};
std::atomic<uint64_t> dependency_allocations{0};
std::atomic<uint64_t> verification_cache_updates{0};
// Handles and ObjPtr use the pinned C++ source ABI. The device implementation
// owns allocation, Heap access and class linking; this hook only selects the
// prevalidated superclass argument. There is no copied allocator implementation.
using AllocateClassFn = art::ObjPtr<art::mirror::Class> (*)(void*,
    art::Handle<art::mirror::Class>, art::Handle<art::mirror::Class>,
    art::Handle<art::mirror::DexCache>, uint16_t);
void* original_allocate_address = nullptr;
bool (*source_transaction)() = nullptr;
thread_local artdcevm::android13::source::HierarchyAllocationScope* allocation_scope = nullptr;
void* original_batch_allocate = nullptr;
void* original_run = nullptr;
}
namespace artdcevm::android13::source {
HierarchyAllocationScope::HierarchyAllocationScope(art::Thread* self,
    std::function<void(const char*)> fail)
    : roots_(self), fail_(std::move(fail)), previous_(allocation_scope) { allocation_scope = this; }
HierarchyAllocationScope::~HierarchyAllocationScope() { allocation_scope = previous_; }
HierarchyAllocationScope* HierarchyAllocationScope::Current() { return allocation_scope; }
void HierarchyAllocationScope::Add(void* owner, art::ObjPtr<art::mirror::Class> old_class,
    art::ObjPtr<art::mirror::DexCache> cache, bool structural) {
  targets_.push_back({owner, roots_.NewHandle(old_class), roots_.NewHandle(cache), structural});
}
art::ObjPtr<art::mirror::Class> HierarchyAllocationScope::Allocate(void* owner,
    art::Handle<art::mirror::Class> old_class,
    art::Handle<art::mirror::DexCache> cache, uint16_t index) {
  if (!Prepared()) { fail_("The subtree allocation hook did not prepare the batch"); return nullptr; }
  for (const auto& replacement : replacements_) {
    if (replacement.old_class.Get() == old_class.Get()) {
      if (replacement.new_class.IsNull()) fail_("Cycle in the requested batch superclass graph");
      return replacement.new_class.Get();
    }
  }
  const size_t slot = replacements_.size();
  replacements_.push_back({roots_.NewHandle(old_class.Get()),
      art::MutableHandle<art::mirror::Class>(roots_.NewHandle<art::mirror::Class>(nullptr))});
  auto* self = art::Thread::Current();
  const auto* dex = cache->GetDexFile();
  const auto& def = dex->GetClassDef(index);
  const char* requested = dex->StringByTypeIdx(def.superclass_idx_);
  auto loader = roots_.NewHandle(old_class->GetClassLoader());
  auto parent = roots_.NewHandle(art::Runtime::Current()->GetClassLinker()->FindClass(self, requested, loader));
  if (parent.IsNull()) {
    if (self->IsExceptionPending()) self->ClearException();
    fail_("Cannot resolve a superclass during batch allocation");
    return nullptr;
  }
  // Every structurally changed old subtree will be recreated by ART. A newly
  // introduced parent can be an explicit target or an implicit descendant.
  const Target* parent_target = nullptr;
  const Target* structural_root = nullptr;
  for (const auto& target : targets_) {
    if (target.old_class.Get() == parent.Get()) parent_target = &target;
    if (target.structural && parent->IsSubClass(target.old_class.Get())) structural_root = &target;
  }
  art::MutableHandle<art::mirror::Class> selected(roots_.NewHandle(parent.Get()));
  if (structural_root != nullptr) {
    ++dependency_allocations;
    auto parent_cache = parent_target != nullptr ? parent_target->cache : roots_.NewHandle(parent->GetDexCache());
    selected.Assign(Allocate(parent_target != nullptr ? parent_target->owner : structural_root->owner,
                             parent, parent_cache, parent_target != nullptr ? 0 : parent->GetDexClassDefIndex()));
    if (selected.IsNull()) return nullptr;
  }
  {
    std::string child_name, parent_name;
    __android_log_print(ANDROID_LOG_INFO, "ArtDcevmHierarchy",
        "allocate=%s requested=%s originalParentMatches=%d selectedIsOld=%d root=%d target=%d",
        old_class->GetDescriptor(&child_name), requested,
        selected.Get() == parent.Get(), selected.Get() == parent.Get(),
        structural_root != nullptr, parent_target != nullptr);
  }
  if (!old_class->GetSuperClass()->DescriptorEquals(requested)) ++parent_selections;
  auto result = reinterpret_cast<AllocateClassFn>(original_allocate_address)(owner, old_class, selected, cache, index);
  replacements_[slot].new_class.Assign(result);
  return result;
}
art::ObjPtr<art::mirror::Class> HierarchyAllocationScope::Find(const char* name,
    art::ObjPtr<art::mirror::ClassLoader> loader) {
  // ART also recreates obsolete generations in the subtree. They have the
  // same descriptor and loader as the live class, but are allocated before
  // the current generation. Verification must see the newest replacement.
  for (auto it = replacements_.rbegin(); it != replacements_.rend(); ++it) {
    const auto& klass = it->new_class;
    if (!klass.IsNull() && klass->GetClassLoader() == loader && klass->DescriptorEquals(name)) {
      ++verifier_resolutions;
      return klass.Get();
    }
  }
  return nullptr;
}
void HierarchyAllocationScope::RefreshVerificationCache(art::Handle<art::mirror::DexCache> cache) {
  bool owned = false;
  for (const auto& target : targets_) owned |= target.cache.Get() == cache.Get();
  if (!owned) return;  // Implicit descendants can still share the live, old DexCache.
  // Linking may already have resolved entries against old classes. These caches
  // belong exclusively to unpublished target definitions; no live cache is
  // modified before commit. Use the pinned source's typed cache accessors.
  auto* types = cache->GetResolvedTypes();
  for (uint32_t i = 0; types && i < cache->NumResolvedTypes(); ++i) {
    auto pair = types[i].load(std::memory_order_relaxed);
    auto klass = pair.object.Read();
    if (klass == nullptr) continue;
    std::string storage;
    auto replacement = Find(klass->GetDescriptor(&storage), klass->GetClassLoader());
    if (replacement != nullptr && replacement != klass) {
      types[i].store(art::mirror::TypeDexCachePair(replacement, pair.index), std::memory_order_release);
      ++verification_cache_updates;
    }
  }
  auto* methods = cache->GetResolvedMethods();
  for (uint32_t i = 0; methods && i < cache->NumResolvedMethods(); ++i)
    art::mirror::DexCache::SetNativePair(methods, i, art::mirror::MethodDexCachePair(
        nullptr, art::mirror::MethodDexCachePair::InvalidIndexForSlot(i)));
  auto* fields = cache->GetResolvedFields();
  for (uint32_t i = 0; fields && i < cache->NumResolvedFields(); ++i)
    art::mirror::DexCache::SetNativePair(fields, i, art::mirror::FieldDexCachePair(
        nullptr, art::mirror::FieldDexCachePair::InvalidIndexForSlot(i)));
  art::WriteBarrier::ForEveryFieldWrite(cache.Get());
}
}
extern "C" bool SourceSubtreeAllocate(void*, void*, void*)
    __asm__("_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE");
extern "C" jvmtiError SourceHierarchyRun(void*)
    __asm__("_ZN12openjdkjvmti15SourceRedefiner3RunEv");
extern "C" void** ArtDcevmSourceConfigureHierarchyBatch(int index) {
  return index == 0 ? &original_batch_allocate : &original_run;
}
extern "C" bool ArtDcevmSourceOriginalHierarchySubtree(void* definition, void* holder, void* data) {
  using Fn = bool (*)(void*, void*, void*);
  return reinterpret_cast<Fn>(original_batch_allocate)(definition, holder, data);
}
extern "C" jvmtiError ArtDcevmSourceOriginalHierarchyRun(void* driver) {
  return reinterpret_cast<jvmtiError (*)(void*)>(original_run)(driver);
}
extern "C" bool ArtDcevmSourceHierarchyAllocateSubtree(void* definition, void* holder, void* data) {
  if (!source_transaction() || !artdcevm::android13::source::HasHierarchyChange())
    return ArtDcevmSourceOriginalHierarchySubtree(definition, holder, data);
  return SourceSubtreeAllocate(definition, holder, data);
}
extern "C" jvmtiError ArtDcevmSourceHierarchyRun(void* driver) {
  if (!source_transaction()) return ArtDcevmSourceOriginalHierarchyRun(driver);
  return SourceHierarchyRun(driver);
}
extern "C" void** ArtDcevmSourceConfigureHierarchyAllocator(bool (*active)()) {
  source_transaction = active;
  return &original_allocate_address;
}
extern "C" art::ObjPtr<art::mirror::Class> ArtDcevmSourceHierarchyAllocator(void* redef,
    art::Handle<art::mirror::Class> old_class, art::Handle<art::mirror::Class> super_class,
    art::Handle<art::mirror::DexCache> cache, uint16_t index) {
  auto original_allocate = reinterpret_cast<AllocateClassFn>(original_allocate_address);
  if (!source_transaction() || !artdcevm::android13::source::HasHierarchyChange())
    return original_allocate(redef, old_class, super_class, cache, index);
  if (allocation_scope) return allocation_scope->Allocate(redef, old_class, cache, index);
  const auto* dex = cache->GetDexFile();
  const auto& def = dex->GetClassDef(index);
  const char* requested = dex->StringByTypeIdx(def.superclass_idx_);
  if (super_class->DescriptorEquals(requested))
    return original_allocate(redef, old_class, super_class, cache, index);
  auto* self = art::Thread::Current();
  art::StackHandleScope<2> hs(self);
  auto loader = hs.NewHandle(old_class->GetClassLoader());
  auto parent = hs.NewHandle(art::Runtime::Current()->GetClassLinker()->FindClass(self, requested, loader));
  // Validation loaded this class before class definitions were paused.
  CHECK(!parent.IsNull());
  CHECK(parent->IsInitialized());
  ++parent_selections;
  return original_allocate(redef, old_class, parent, cache, index);
}

namespace {
using Verify = decltype(&art::verifier::ClassVerifier::VerifyClass);
using Resolve = art::ObjPtr<art::mirror::Class> (*)(void*, const char*, art::ObjPtr<art::mirror::ClassLoader>);
void* original_verify = nullptr;
void* original_resolve = nullptr;
void* original_find = nullptr;
void* original_lookup = nullptr;
// Verification sees the unpublished replacement through a thread-local view.
// The borrowed Handle remains rooted by CheckVerification for the entire call.
struct VerificationView {
  const art::Handle<art::mirror::Class>* klass;
  VerificationView* previous;
};
thread_local VerificationView* verification_view = nullptr;
}
extern "C" void** ArtDcevmSourceConfigureHierarchyVerification(int index) {
  switch (index) {
    case 0: return &original_verify;
    case 1: return &original_resolve;
    case 2: return &original_find;
    default: return &original_lookup;
  }
}
extern "C" art::verifier::FailureKind ArtDcevmSourceHierarchyVerify(
    art::Thread* self, art::verifier::VerifierDeps* deps, const art::DexFile* dex,
    art::Handle<art::mirror::Class> klass, art::Handle<art::mirror::DexCache> cache,
    art::Handle<art::mirror::ClassLoader> loader, const art::dex::ClassDef& def,
    art::CompilerCallbacks* callbacks, art::verifier::HardFailLogMode log_level,
    uint32_t api, std::string* error) {
  auto original = reinterpret_cast<Verify>(original_verify);
  if (!source_transaction() || klass.IsNull())
    return original(self, deps, dex, klass, cache, loader, def, callbacks, log_level, api, error);
  VerificationView view{&klass, verification_view};
  ++verifier_entries;
  verification_view = &view;
  if (allocation_scope) allocation_scope->RefreshVerificationCache(cache);
  if (allocation_scope && klass->GetSuperClass() != nullptr) {
    std::string name, parent_name;
    auto parent = klass->GetSuperClass();
    auto selected = allocation_scope->Find(parent->GetDescriptor(&parent_name), parent->GetClassLoader());
    __android_log_print(ANDROID_LOG_INFO, "ArtDcevmHierarchy",
        "verify=%s parent=%s tracked=%d selectedMatches=%d selfMatches=%d cacheUpdates=%llu",
        klass->GetDescriptor(&name), parent->GetDescriptor(&parent_name), selected != nullptr,
        selected == nullptr || selected == parent,
        allocation_scope->Find(klass->GetDescriptor(&name), klass->GetClassLoader()) == klass.Get(),
        static_cast<unsigned long long>(verification_cache_updates.load()));
  }
  auto result = original(self, deps, dex, klass, cache, loader, def, callbacks, log_level, api, error);
  verification_view = view.previous;
  return result;
}
extern "C" art::ObjPtr<art::mirror::Class> ArtDcevmSourceHierarchyResolve(
    void* type_cache, const char* descriptor, art::ObjPtr<art::mirror::ClassLoader> loader) {
  for (auto* view = verification_view; view; view = view->previous) {
    for (art::ObjPtr<art::mirror::Class> klass(view->klass->Get());
         klass != nullptr; klass = klass->GetSuperClass()) {
      if (klass->GetClassLoader() == loader && klass->DescriptorEquals(descriptor)) {
        ++verifier_resolutions;
        return klass;
      }
    }
  }
  return reinterpret_cast<Resolve>(original_resolve)(type_cache, descriptor, loader);
}
extern "C" uint64_t ArtDcevmSourceHierarchyDiagnostics(int index) {
  return index == 0 ? parent_selections.load() : index == 1 ? verifier_resolutions.load()
      : index == 2 ? verifier_entries.load() : index == 3 ? dependency_allocations.load()
      : verification_cache_updates.load();
}
namespace {
art::ObjPtr<art::mirror::Class> VerificationClass(const char* descriptor,
    art::ObjPtr<art::mirror::ClassLoader> loader) REQUIRES_SHARED(art::Locks::mutator_lock_) {
  for (auto* view = verification_view; view; view = view->previous) {
    // Multiple obsolete generations share descriptor and loader. The class
    // under verification already points at its exact unpublished ancestry.
    for (art::ObjPtr<art::mirror::Class> klass(view->klass->Get());
         klass != nullptr; klass = klass->GetSuperClass()) {
      if (klass->GetClassLoader() == loader && klass->DescriptorEquals(descriptor)) {
        ++verifier_resolutions;
        return klass;
      }
    }
  }
  return verification_view && allocation_scope ? allocation_scope->Find(descriptor, loader) : nullptr;
}
}
// ResolveClass can be inlined into RegTypeCache::From by the device compiler.
// Intercept its named linker calls too, under the same verifier-only view.
extern "C" art::ObjPtr<art::mirror::Class> ArtDcevmSourceHierarchyFind(
    void* linker, art::Thread* self, const char* descriptor, art::Handle<art::mirror::ClassLoader> loader) {
  auto replacement = VerificationClass(descriptor, loader.Get());
  if (replacement != nullptr) return replacement;
  using Fn = art::ObjPtr<art::mirror::Class> (*)(void*, art::Thread*, const char*, art::Handle<art::mirror::ClassLoader>);
  return reinterpret_cast<Fn>(original_find)(linker, self, descriptor, loader);
}
extern "C" art::ObjPtr<art::mirror::Class> ArtDcevmSourceHierarchyLookup(
    void* linker, art::Thread* self, const char* descriptor, art::ObjPtr<art::mirror::ClassLoader> loader) {
  auto replacement = VerificationClass(descriptor, loader);
  if (replacement != nullptr) return replacement;
  using Fn = art::ObjPtr<art::mirror::Class> (*)(void*, art::Thread*, const char*, art::ObjPtr<art::mirror::ClassLoader>);
  return reinterpret_cast<Fn>(original_lookup)(linker, self, descriptor, loader);
}

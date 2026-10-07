#include "platform/android13/async_deopt_hooks.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/symbols/android13.h"
#include <atomic>
#include <cstring>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
using IsAsyncDeoptimizeable = bool (*)(const void*, uintptr_t);
IsAsyncDeoptimizeable original_is_async;
void* (*root)(void*);
void* (*obsolete_cache)(void*);
const void* (*oat_code)(void*,size_t);
bool (*debuggable)(const void*);
std::atomic<bool> probe_enabled{false};
std::atomic<uint64_t> hits{0};
template<class T> T Read(const void* p,size_t offset) {
  T value; std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value)); return value;
}
struct AotVisitor final:stack_abi::Visitor {
  AotVisitor(void* self,uintptr_t target):Visitor(self,nullptr,stack_abi::WalkKind::kSkipInlined),target(target) {}
  bool VisitFrame() override {
    if (!quick_frame || pc!=target) return true;
    void* method=Method();
    if (!method || Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
        (Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,access_flags))&0x100)) return false;
    void* klass=root(method);
    if (!klass || !ReadHeapReference(klass,offsetof(validation_abi::ClassPrefix,class_loader))) return false;
    void* cache;
    if (Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,access_flags))&0x40000) cache=obsolete_cache(method);
    else {
      // GetDexFile deliberately reads the immutable cache link without a barrier.
      cache=reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,dex_cache))));
    }
    const void* dex=cache?reinterpret_cast<void*>(Read<uint64_t>(cache,offsetof(validation_abi::DexCachePrefix,dex_file))):nullptr;
    const void* oat_dex=dex?Read<const void*>(dex,offsetof(stack_abi::DexPrefix,oat_dex)):nullptr;
    const void* oat=oat_dex?Read<const void*>(oat_dex,0):nullptr;
    if (!oat || !debuggable(oat)) return false;
    const void* code=oat_code(method,8); // Matched arm64 PointerSize::k64 profile.
    if (!code) return false;
    // Compare ART's header identity. Vendor code-size bits are never decoded.
    safe=header==static_cast<const unsigned char*>(code)-stack_abi::kCodeOffset && target>=reinterpret_cast<uintptr_t>(code);
    return false;
  }
  uintptr_t target; bool safe=false;
};
bool Allow(const void*,uintptr_t pc) {
  if (!ReceiverGuardRequired() && !probe_enabled.load(std::memory_order_acquire)) return false;
  void* self=CurrentHierarchyThread(); if (!self) return false;
  AotVisitor visitor(self,pc); visitor.Walk();
  if (visitor.safe) hits.fetch_add(1,std::memory_order_relaxed);
  return visitor.safe;
}
bool Proxy(const void* runtime,uintptr_t pc) { return original_is_async(runtime,pc) || Allow(runtime,pc); }
void SetProbe(bool enabled) { probe_enabled.store(enabled,std::memory_order_release); }
uint64_t Hits() { return hits.load(std::memory_order_relaxed); }
}
bool InstallAsyncDeoptHook(HookEngine& hooks,void* source,std::string* error) {
  if (!ConfigureStackAccess(hooks,source,error)) return false;
  auto r=reinterpret_cast<void* (*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto cache=reinterpret_cast<void* (*)(void*)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod19GetObsoleteDexCacheEv"));
  auto code=reinterpret_cast<const void* (*)(void*,size_t)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE"));
  auto debug=reinterpret_cast<bool (*)(const void*)>(hooks.FindSymbol("libart.so","_ZNK3art7OatFile12IsDebuggableEv"));
  using Configure=void (*)(void (*)(bool),uint64_t (*)());
  auto configure=reinterpret_cast<Configure>(dlsym(source,"ArtDcevmSourceConfigureAsyncDeopt"));
  void* target=hooks.FindSymbol("libart.so",cas::kIsAsyncDeoptimizeable);
  if (!r || !cache || !code || !debug || !configure || !target) { *error="Missing named AOT safety functions or diagnostics configuration"; return false; }
  root=r; obsolete_cache=cache; oat_code=code; debuggable=debug;
  configure(SetProbe,Hits);
  return hooks.Install(target,reinterpret_cast<void*>(Proxy),reinterpret_cast<void**>(&original_is_async),error)!=nullptr;
}
}

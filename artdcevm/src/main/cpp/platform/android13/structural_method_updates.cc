#include "platform/android13/structural_method_updates.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/nterp_dispatch.h"
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
using Method = art_method_abi::ArtMethod;
using Class = validation_abi::ClassPrefix;
using Same = bool (*)(void*, void*);
using Root = void* (*)(void*);
using Obsolete = void (*)(void*, void*);
Same same = nullptr; Root root = nullptr; Obsolete obsolete = nullptr;
bool (*probe)(void*) = nullptr;
constexpr uint32_t kStatic=0x8, kNative=0x100, kAbstract=0x400;
constexpr uint32_t kObsolete=0x40000, kDontCompile=0x2000000;
template<class T> T Read(void* base, size_t offset) {
  T value; std::memcpy(&value, static_cast<unsigned char*>(base)+offset, sizeof(value)); return value;
}
uint32_t Flags(void* method) {
  return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)), __ATOMIC_RELAXED);
}
void* Array(void* klass) { return reinterpret_cast<void*>(Read<uint64_t>(klass,offsetof(Class,methods))); }
void* At(void* array, uint32_t index) { return static_cast<unsigned char*>(array)+art_method_abi::kArrayDataOffset+sizeof(Method)*index; }
}
void* FindStructuralMethodReplacement(void* old_method, void* new_class) {
  auto* array=Array(new_class); if (!array) return nullptr;
  const uint32_t count=Read<uint32_t>(array,0), flags=Flags(old_method);
  for (uint32_t i=0;i<count;++i) {
    void* candidate=At(array,i);
    if (((Flags(candidate)^flags)&kStatic)==0 && same(candidate,old_method)) return candidate;
  }
  return nullptr;
}
void UpdateStructuralMethods(void* old_class, void* new_class) {
  if (!probe(old_class) || !probe(new_class)) std::abort();
  auto* array=Array(old_class); if (!array) return;
  const uint32_t count=Read<uint32_t>(array,0);
  for (uint32_t i=0;i<count;++i) {
    void* method=At(array,i);
    // Copied interface defaults retain the live interface as their declaring class.
    if (root(method)!=old_class) continue;
    const uint32_t flags=Flags(method);
    if ((flags&kNative) && !IsRetiredMethodPointer(method)) {
      void* replacement=FindStructuralMethodReplacement(method,new_class);
      if (!replacement || replacement==method) std::abort(); // Mapping is an ART commit invariant.
      if (Flags(replacement)&kNative) {
        uint64_t binding=Read<uint64_t>(method,offsetof(Method,data));
        std::memcpy(static_cast<unsigned char*>(replacement)+offsetof(Method,data),&binding,sizeof(binding));
      }
    }
    __atomic_fetch_or(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)),kObsolete,__ATOMIC_RELAXED);
    obsolete(DispatchClassLinker(),method);
    if (!(Flags(method)&kAbstract))
      __atomic_fetch_or(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)),kDontCompile,__ATOMIC_RELAXED);
  }
}
bool ConfigureStructuralMethodUpdates(HookEngine& hooks, void* implementation, std::string* error) {
  using Configure=void (*)(void (*)(void*,void*));
  auto configure=reinterpret_cast<Configure>(dlsym(implementation,"ArtDcevmSourceConfigureStructuralMethodUpdates"));
  auto p=reinterpret_cast<bool (*)(void*)>(dlsym(implementation,"ArtDcevmSourceProbeStructuralMethodUpdates"));
  auto s=reinterpret_cast<Same>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
  auto r=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto o=reinterpret_cast<Obsolete>(hooks.FindSymbol("libart.so","_ZNK3art11ClassLinker31SetEntryPointsForObsoleteMethodEPNS_9ArtMethodE"));
  if (!configure || !p || !s || !r || !o || !DispatchClassLinker()) {
    *error="Missing named structural method update functions or ClassLinker"; return false;
  }
  same=s; root=r; obsolete=o; probe=p; configure(UpdateStructuralMethods); return true;
}
}

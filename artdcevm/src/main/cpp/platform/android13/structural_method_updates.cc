#include "platform/android13/structural_method_updates.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/nterp_dispatch.h"
#include <cstring>
#include <cstdlib>
#include "platform/android13/transaction_metadata.h"
namespace artdcevm::android13 {
namespace {
using Method = art_method_abi::ArtMethod;
using Same = bool (*)(void*, void*);
using Root = void* (*)(void*);
using Obsolete = void (*)(void*, void*);
Same same = nullptr; Root root = nullptr; Obsolete obsolete = nullptr;
constexpr uint32_t kStatic=0x8, kNative=0x100, kAbstract=0x400;
constexpr uint32_t kObsolete=0x40000, kDontCompile=0x2000000;
template<class T> T Read(void* base, size_t offset) {
  T value; std::memcpy(&value, static_cast<unsigned char*>(base)+offset, sizeof(value)); return value;
}
uint32_t Flags(void* method) {
  return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)), __ATOMIC_RELAXED);
}
void* At(void* array, uint32_t index) { return static_cast<unsigned char*>(array)+art_method_abi::kArrayDataOffset+sizeof(Method)*index; }
}
void* FindStructuralMethodReplacement(void* old_method, void* new_class) {
  MethodStorageView storage;std::string error;
  if(!GetTransactionMetadataReader().ReadMethods(new_class,&storage,&error))std::abort();
  auto* array=storage.array; if (!array) return nullptr;
  const uint32_t count=storage.count, flags=Flags(old_method);
  for (uint32_t i=0;i<count;++i) {
    void* candidate=At(array,i);
    if (((Flags(candidate)^flags)&kStatic)==0 && same(candidate,old_method)) return candidate;
  }
  return nullptr;
}
void UpdateStructuralMethods(void* old_class, void* new_class) {
  MethodStorageView storage,next;std::string error;
  if(!GetTransactionMetadataReader().ReadMethods(old_class,&storage,&error)||
     !GetTransactionMetadataReader().ReadMethods(new_class,&next,&error))std::abort();
  auto* array=storage.array; if (!array) return;
  const uint32_t count=storage.count;
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
bool ConfigureStructuralMethodUpdates(HookEngine& hooks, std::string* error) {
  auto s=reinterpret_cast<Same>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
  auto r=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto o=reinterpret_cast<Obsolete>(hooks.FindSymbol("libart.so","_ZNK3art11ClassLinker31SetEntryPointsForObsoleteMethodEPNS_9ArtMethodE"));
  if (!s || !r || !o || !DispatchClassLinker()) {
    *error="Missing named structural method update functions or ClassLinker"; return false;
  }
  same=s; root=r; obsolete=o; return true;
}
}

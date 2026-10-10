#include "platform/android13/structural_method_updates.h"
#include "platform/common/structural_methods.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
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
template<class T> T Read(void* base, size_t offset) {
  T value; std::memcpy(&value, static_cast<unsigned char*>(base)+offset, sizeof(value)); return value;
}
uint32_t Flags(void* method) {
  return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)), __ATOMIC_RELAXED);
}
void* At(void* array, uint32_t index) { return static_cast<unsigned char*>(array)+art_method_abi::kArrayDataOffset+sizeof(Method)*index; }
}
namespace {
class NativeMethods final:public structural_methods::Services {
 public:
  NativeMethods(void* old_class,void* new_class):old_class_(old_class),new_class_(new_class),
      old_storage_(old_class?ReadStorage(old_class):MethodStorageView{}),
      new_storage_(ReadStorage(new_class)) {}
  size_t Count(void* klass)const override{return Storage(klass).count;}
  void* At(void* klass,size_t index)const override {
    auto storage=Storage(klass);if(index>=storage.count)std::abort();
    return android13::At(storage.array,static_cast<uint32_t>(index));
  }
  uint32_t Flags(void* method)const override{return android13::Flags(method);}
  void* DeclaringClass(void* method)const override{return root(method);}
  bool SameIdentity(void* a,void* b)const override{return same(a,b);}
  bool IsRetired(void* method)const override{return IsRetiredMethodPointer(method);}
  void TransferNativeBinding(void* source,void* target)const override {
    uint64_t binding=Read<uint64_t>(source,offsetof(Method,data));
    std::memcpy(static_cast<unsigned char*>(target)+offsetof(Method,data),&binding,sizeof(binding));
  }
  void AddFlags(void* method,uint32_t flags)const override {
    __atomic_fetch_or(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)),flags,__ATOMIC_RELAXED);
  }
  void SetObsoleteEntry(void* method)const override{obsolete(DispatchClassLinker(),method);}
 private:
  const MethodStorageView& Storage(void* klass)const {
    if(klass==new_class_)return new_storage_;
    if(klass==old_class_&&old_class_)return old_storage_;
    std::abort();
  }
  static MethodStorageView ReadStorage(void* klass) {
    MethodStorageView storage;std::string error;
    if(!GetTransactionMetadataReader().ReadMethods(klass,&storage,&error))std::abort();
    return storage;
  }
  void* old_class_;
  void* new_class_;
  MethodStorageView old_storage_;
  MethodStorageView new_storage_;
};
}
void* FindStructuralMethodReplacement(void* old_method,void* new_class) {
  return structural_methods::FindReplacement(NativeMethods{nullptr,new_class},old_method,new_class);
}
void UpdateStructuralMethods(void* old_class,void* new_class) {
  structural_methods::Update(NativeMethods{old_class,new_class},old_class,new_class);
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

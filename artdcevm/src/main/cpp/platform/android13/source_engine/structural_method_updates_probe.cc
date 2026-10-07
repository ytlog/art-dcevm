#include "art_method-inl.h"
#include "mirror/class-inl.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/jni_method_targets_abi.h"
#include "scoped_thread_state_change-inl.h"
#include <cstring>
#include <initializer_list>
namespace {
template<class T> T Read(void* base,size_t offset) { T value; std::memcpy(&value,static_cast<unsigned char*>(base)+offset,sizeof(value)); return value; }
}
extern "C" bool ArtDcevmSourceProbeJniMethodTargets(
    artdcevm::android13::JniMethodTarget canonicalize) {
  static_assert(sizeof(art::PointerSize)==sizeof(size_t));
  static_assert(static_cast<size_t>(art::PointerSize::k64)==8);
  static_assert(art::ArtMethod::AccessFlagsOffset().Int32Value()==
      offsetof(artdcevm::android13::art_method_abi::ArtMethod,access_flags));
  static_assert(art::kAccCopied==0x1000000 && art::kAccIntrinsic==0x80000000);
  if (!canonicalize || art::kIsDebugBuild) return false;
  art::ScopedObjectAccess soa(art::Thread::Current());
  // These unpublished native declarations only exercise passthrough flags;
  // calling GetCanonicalMethod on a fabricated copied method would be invalid.
  art::ArtMethod method;
  for (uint32_t flags : {uint32_t{0}, art::kAccIntrinsic, art::kAccCopied | art::kAccIntrinsic}) {
    method.SetAccessFlags(flags);
    if (method.IsCopied() || canonicalize(&method)!=&method) return false;
  }
  return true;
}
extern "C" bool ArtDcevmSourceProbeStructuralMethodUpdates(void* raw)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  using Method=artdcevm::android13::art_method_abi::ArtMethod;
  using Class=artdcevm::android13::validation_abi::ClassPrefix;
  static_assert(art::ArtMethod::Size(art::PointerSize::k64)==sizeof(Method));
  static_assert(art::ArtMethod::DeclaringClassOffset().Int32Value()==offsetof(Method,declaring_class));
  static_assert(art::ArtMethod::AccessFlagsOffset().Int32Value()==offsetof(Method,access_flags));
  static_assert(art::ArtMethod::DataOffset(art::PointerSize::k64).Int32Value()==offsetof(Method,data));
  static_assert(art::mirror::Class::MethodsOffset().Int32Value()==offsetof(Class,methods));
  static_assert(art::kAccStatic==0x8 && art::kAccNative==0x100 && art::kAccAbstract==0x400);
  static_assert(art::kAccObsoleteMethod==0x40000 && art::kAccCompileDontBother==0x2000000);
  static_assert(art::kAccCopied==0x1000000 && art::kAccIntrinsic==0x80000000);
  auto* klass=static_cast<art::mirror::Class*>(raw);
  auto methods=klass->GetMethods(art::PointerSize::k64);
  auto* array=reinterpret_cast<void*>(Read<uint64_t>(raw,offsetof(Class,methods)));
  if (!array) return methods.empty();
  if (Read<uint32_t>(array,0)!=methods.size()) return false;
  size_t index=0;
  for (auto& method:methods) {
    auto* fixed=static_cast<unsigned char*>(array)+artdcevm::android13::art_method_abi::kArrayDataOffset+sizeof(Method)*index++;
    if (fixed!=reinterpret_cast<void*>(&method) || Read<uint32_t>(fixed,4)!=method.GetAccessFlags() ||
        Read<uint64_t>(fixed,offsetof(Method,data))!=reinterpret_cast<uint64_t>(method.GetDataPtrSize(art::PointerSize::k64))) return false;
  }
  return true;
}

#include "platform/android13/art_method_abi.h"
#include "platform/android13/loaded_class_guards_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "art_method.h"
#include "base/length_prefixed_array.h"
#include "class_linker.h"
#include "mirror/array.h"
#include "mirror/iftable.h"
#include <type_traits>
using FixedMethod = artdcevm::android13::art_method_abi::ArtMethod;
static_assert(art::ArtMethod::Size(art::PointerSize::k64) == sizeof(FixedMethod));
static_assert(art::ArtMethod::Alignment(art::PointerSize::k64) == alignof(FixedMethod));
static_assert(art::ArtMethod::DeclaringClassOffset().Uint32Value() == offsetof(FixedMethod,declaring_class));
static_assert(art::ArtMethod::AccessFlagsOffset().Uint32Value() == offsetof(FixedMethod,access_flags));
static_assert(art::ArtMethod::DexMethodIndexOffset().Uint32Value() == offsetof(FixedMethod,dex_method_index));
static_assert(art::ArtMethod::DataOffset(art::PointerSize::k64).Uint32Value() == offsetof(FixedMethod,data));
static_assert(art::mirror::Array::LengthOffset().Uint32Value() == 8);
static_assert(art::mirror::Array::DataOffset(4).Uint32Value() == 12);
static_assert(art::mirror::IfTable::kInterface == 0 && art::mirror::IfTable::kMax == 2);
static_assert(sizeof(art::ClassVisitor) == sizeof(artdcevm::android13::loaded_class_abi::ClassVisitor));
static_assert(alignof(art::ClassVisitor) == alignof(artdcevm::android13::loaded_class_abi::ClassVisitor));
static_assert(sizeof(art::ObjPtr<art::mirror::Class>) == sizeof(uintptr_t));
static_assert(std::is_trivially_copyable_v<art::ObjPtr<art::mirror::Class>>);
extern "C" int ArtDcevmSourceProbeLoadedClassGuards() {
  if (art::kIsDebugBuild || art::kObjPtrPoisoning || art::kPoisonHeapReferences) return 1;
  return art::LengthPrefixedArray<art::ArtMethod>::OffsetOfElement(0,sizeof(FixedMethod),alignof(FixedMethod)) ==
      artdcevm::android13::art_method_abi::kArrayDataOffset ? 0 : 2;
}

#include "ti_redefine.h"
#include "mirror/class-inl.h"
#include "platform/android13/method_validation_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "mirror/dex_cache.h"
#include <type_traits>

namespace art {
struct ClassOffsets {
  static void VerifyValidation() {
    using Fixed = artdcevm::android13::validation_abi::ClassPrefix;
    static_assert(offsetof(mirror::Class, dex_cache_) == offsetof(Fixed, dex_cache));
    static_assert(offsetof(mirror::Class, class_loader_) == offsetof(Fixed, class_loader));
    static_assert(offsetof(mirror::Class, super_class_) == offsetof(Fixed, super_class));
    static_assert(offsetof(mirror::Class, ifields_) == offsetof(Fixed, ifields));
    static_assert(offsetof(mirror::Class, sfields_) == offsetof(Fixed, sfields));
    static_assert(offsetof(mirror::Class, methods_) == offsetof(Fixed, methods));
    static_assert(offsetof(mirror::Class, iftable_) == offsetof(Fixed, iftable));
    static_assert(offsetof(mirror::Class, vtable_) == offsetof(Fixed, vtable));
    static_assert(offsetof(mirror::Class, primitive_type_) == offsetof(Fixed, primitive_type));
    static_assert(offsetof(mirror::Class, status_) == offsetof(Fixed, status));
    static_assert(offsetof(mirror::Class, copied_methods_offset_) == offsetof(Fixed, copied_methods_offset));
    static_assert(offsetof(mirror::Class, virtual_methods_offset_) == offsetof(Fixed, virtual_methods_offset));
    static_assert(offsetof(mirror::Class, dex_type_idx_) == offsetof(Fixed, dex_type_index));
    static_assert(offsetof(mirror::Class, access_flags_) == offsetof(Fixed, access_flags));
    static_assert(offsetof(mirror::Class, dex_class_def_idx_) == offsetof(Fixed, dex_class_def_index));
    static_assert(sizeof(mirror::Object) == sizeof(artdcevm::android13::validation_abi::ObjectPrefix));
    static_assert(sizeof(mirror::HeapReference<mirror::DexCache>) == sizeof(uint32_t));
    static_assert(alignof(mirror::Class) == alignof(Fixed));
    static_assert(sizeof(ObjPtr<mirror::Class>) == sizeof(uintptr_t));
    static_assert(std::is_trivially_copyable_v<ObjPtr<mirror::Class>>);
  }
};
struct DexCacheOffsets {
  static void VerifyValidation() {
    using Fixed = artdcevm::android13::validation_abi::DexCachePrefix;
    static_assert(offsetof(mirror::DexCache, dex_file_) == offsetof(Fixed, dex_file));
    static_assert(alignof(mirror::DexCache) == alignof(Fixed));
  }
};
}
namespace {
artdcevm::android13::MethodValidation validate = nullptr;
struct DexMappingLayoutProbe : art::DexFile {
  static void Verify() {
    using Fixed = artdcevm::android13::validation_abi::DexMappingPrefix;
    static_assert(offsetof(DexMappingLayoutProbe, begin_) == offsetof(Fixed, begin));
    static_assert(offsetof(DexMappingLayoutProbe, size_) == offsetof(Fixed, size));
    static_assert(offsetof(DexMappingLayoutProbe, data_begin_) == offsetof(Fixed, data_begin));
    static_assert(offsetof(DexMappingLayoutProbe, data_size_) == offsetof(Fixed, data_size));
  }
};
}
extern "C" int ArtDcevmSourceConfigureValidationMetadata() {
  art::ClassOffsets::VerifyValidation();
  art::DexCacheOffsets::VerifyValidation();
  DexMappingLayoutProbe::Verify();
  if (art::kObjPtrPoisoning || art::kPoisonHeapReferences) return 1;
  return 0;
}
extern "C" void ArtDcevmSourceConfigureMethodValidation(artdcevm::android13::MethodValidation callback) {
  validate = callback;
}
extern "C" bool ArtDcevmSourceCheckMethods(void* definition) { return validate(definition); }
namespace openjdkjvmti {
struct MethodValidationBridge {
  // Transitional calibration only: compare the owned reader with source getters
  // on each real transaction, including shared-data CompactDex mappings. No
  // source data is returned to the enhanced validators or creates member IDs.
  static bool CompareMetadata(void* definition, const artdcevm::android13::ValidationMetadata& owned) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    auto klass = redef->GetMirrorClass();
    const art::DexFile& dex = klass->GetDexFile();
    return
        owned.dex.begin == dex.Begin() && owned.dex.size == dex.Size() &&
        owned.dex.data_begin == dex.DataBegin() && owned.dex.data_size == dex.DataSize() &&
        owned.dex.class_def_index == klass->GetDexClassDefIndex() &&
        owned.access_flags == klass->GetAccessFlags();
  }
};
}
extern "C" bool ArtDcevmSourceProbeValidationMetadata(void* definition,
    const artdcevm::android13::ValidationMetadata* owned) {
  return openjdkjvmti::MethodValidationBridge::CompareMetadata(definition, *owned);
}

#include "ti_redefine.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "mirror/class-inl.h"
#include "platform/android13/method_validation_abi.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "platform/android13/validation_metadata_abi.h"
#include "mirror/dex_cache.h"
#include "runtime.h"
#include <type_traits>

namespace art {
struct ClassOffsets {
  static void VerifyValidation() {
    using Fixed = artdcevm::android13::validation_abi::ClassPrefix;
    static_assert(offsetof(mirror::Class, dex_cache_) == offsetof(Fixed, dex_cache));
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
  static bool Guard(void* definition, const char* name, const char* descriptor,
                    uint32_t old_flags, int action) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    art::StackHandleScope<1> hs(redef->driver_->self_);
    auto klass = hs.NewHandle(redef->GetMirrorClass());
    using Action = artdcevm::android13::ModifierEvolutionAction;
    const bool final = action == static_cast<int>(Action::kCheckFinalOverride);
    bool invalid = false;
    art::ClassFuncVisitor visitor([&](art::ObjPtr<art::mirror::Class> subtype)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      if (subtype == klass.Get() || !subtype->IsLoaded() || !klass->IsAssignableFrom(subtype))
        return true;
      if (final) {
        if (!subtype->CanAccessMember(klass.Get(), old_flags)) return true;
        for (art::ArtMethod& candidate : subtype->GetVirtualMethods(art::kRuntimePointerSize)) {
          if (candidate.GetNameView() == name && candidate.GetSignature().ToString() == descriptor) {
            invalid = true;
            return false;
          }
        }
      } else {
        if (subtype->IsAbstract()) return true;
        bool concrete = false;
        for (auto cursor = subtype; cursor != klass.Get(); cursor = cursor->GetSuperClass()) {
          for (art::ArtMethod& candidate : cursor->GetVirtualMethods(art::kRuntimePointerSize)) {
            if (candidate.GetDeclaringClass() == cursor && candidate.GetNameView() == name &&
                candidate.GetSignature().ToString() == descriptor) {
              concrete = !candidate.IsAbstract();
              break;
            }
          }
          if (concrete) break;
        }
        if (!concrete) { invalid = true; return false; }
      }
      return true;
    });
    redef->driver_->runtime_->GetClassLinker()->VisitClasses(&visitor);
    return !invalid;
  }
};
}
extern "C" bool ArtDcevmSourceProbeValidationMetadata(void* definition,
    const artdcevm::android13::ValidationMetadata* owned) {
  return openjdkjvmti::MethodValidationBridge::CompareMetadata(definition, *owned);
}
extern "C" bool ArtDcevmSourceMethodDependentGuard(void* definition, const char* name,
    const char* descriptor, uint32_t old_flags, int action) {
  return openjdkjvmti::MethodValidationBridge::Guard(definition, name, descriptor, old_flags, action);
}

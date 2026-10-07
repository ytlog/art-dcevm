#include "ti_redefine.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "mirror/class-inl.h"
#include "platform/android13/method_validation_abi.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "runtime.h"

namespace { artdcevm::android13::MethodValidation validate = nullptr; }
extern "C" void ArtDcevmSourceConfigureMethodValidation(artdcevm::android13::MethodValidation callback) {
  validate = callback;
}
extern "C" bool ArtDcevmSourceCheckMethods(void* definition) { return validate(definition); }
namespace openjdkjvmti {
struct MethodValidationBridge {
  // This bridge borrows the verified DEX mapping only. Metadata enumeration and
  // validation stay in the enhancement without materializing JNI member IDs.
  static bool Metadata(void* definition, artdcevm::evolution::MappedDexMetadata* output) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    auto klass = redef->GetMirrorClass();
    const art::DexFile& dex = klass->GetDexFile();
    *output = {dex.Begin(), dex.Size(), dex.DataBegin(), dex.DataSize(), klass->GetDexClassDefIndex()};
    return true;
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
extern "C" bool ArtDcevmSourceReadOldDexMetadata(void* definition,
    artdcevm::evolution::MappedDexMetadata* output) {
  return openjdkjvmti::MethodValidationBridge::Metadata(definition, output);
}
extern "C" bool ArtDcevmSourceMethodDependentGuard(void* definition, const char* name,
    const char* descriptor, uint32_t old_flags, int action) {
  return openjdkjvmti::MethodValidationBridge::Guard(definition, name, descriptor, old_flags, action);
}

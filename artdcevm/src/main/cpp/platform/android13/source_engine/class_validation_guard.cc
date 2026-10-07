#include "ti_redefine.h"
#include "class_linker-inl.h"
#include "dex/dex_file-inl.h"
#include "hierarchy.h"
#include "mirror/class-inl.h"
#include "platform/android13/class_validation_abi.h"
#include "runtime.h"

namespace {
static_assert(art::kAccValidClassFlags == artdcevm::android13::kValidationClassFlags);
static_assert(art::kAccValidInterfaceFlags == artdcevm::android13::kValidationInterfaceFlags);
artdcevm::android13::ClassValidation validate = nullptr;
thread_local std::string hierarchy_error;
}
extern "C" void ArtDcevmSourceConfigureClassValidation(artdcevm::android13::ClassValidation callback) {
  validate = callback;
}
extern "C" bool ArtDcevmSourceCheckClass(void* definition) { return validate(definition); }
namespace openjdkjvmti {
struct ClassValidationBridge {
  static uint32_t Flags(void* definition) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    // ClassGetModifiers also reads InnerClass annotations. Keep the original
    // CheckClass contract of Class access flags masked by valid class bits.
    return redef->GetMirrorClass()->GetAccessFlags() & art::kAccValidClassFlags;
  }
  static bool Subclasses(void* definition) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    art::StackHandleScope<1> hs(redef->driver_->self_);
    auto current = hs.NewHandle(redef->GetMirrorClass());
    bool loaded = false;
    art::ClassFuncVisitor visitor([&](art::ObjPtr<art::mirror::Class> subtype)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      if (subtype != current.Get() && subtype->IsLoaded() && current->IsAssignableFrom(subtype)) {
        loaded = true; return false;
      }
      return true;
    });
    redef->driver_->runtime_->GetClassLinker()->VisitClasses(&visitor);
    return !loaded;
  }
  static int Hierarchy(void* definition, bool* changed, const char** error) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    art::StackHandleScope<2> hs(redef->driver_->self_);
    auto current = hs.NewHandle(redef->GetMirrorClass());
    art::MutableHandle<art::mirror::Class> parent(hs.NewHandle(current->GetSuperClass()));
    hierarchy_error.clear();
    auto result = artdcevm::android13::source::CheckHierarchyAdditions(
        current, *redef->dex_file_, &parent, changed, &hierarchy_error);
    if (result == JVMTI_ERROR_NONE && !parent->DescriptorEquals(redef->dex_file_->StringByTypeIdx(
        redef->dex_file_->GetClassDef(0).superclass_idx_))) {
      result = JVMTI_ERROR_INTERNAL;
      hierarchy_error = "Hierarchy validation selected a different superclass";
    }
    if (result == JVMTI_ERROR_NONE && *changed &&
        artdcevm::android13::source::HierarchyAllocationScope::Current() == nullptr) {
      result = JVMTI_ERROR_NOT_AVAILABLE;
      hierarchy_error = "The source transaction hook did not establish allocation context";
    }
    *error = hierarchy_error.c_str();
    return result;
  }
};
}
extern "C" uint32_t ArtDcevmSourceClassDeclarationFlags(void* definition) {
  return openjdkjvmti::ClassValidationBridge::Flags(definition);
}
extern "C" bool ArtDcevmSourceClassSubclassGuard(void* definition) {
  return openjdkjvmti::ClassValidationBridge::Subclasses(definition);
}
extern "C" int ArtDcevmSourceClassHierarchyGuard(void* definition, bool* changed, const char** error) {
  return openjdkjvmti::ClassValidationBridge::Hierarchy(definition, changed, error);
}

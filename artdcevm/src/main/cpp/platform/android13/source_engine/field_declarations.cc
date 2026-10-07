#include "ti_redefine.h"
#include "dex/class_accessor-inl.h"
#include "art_field-inl.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "retired_fields.h"
#include "platform/android13/field_validation_abi.h"

namespace {
artdcevm::android13::FieldValidation validate = nullptr;
}
extern "C" void ArtDcevmSourceConfigureFieldValidation(
    artdcevm::android13::FieldValidation callback) { validate = callback; }
extern "C" bool ArtDcevmSourceCheckFields(void* definition) { return validate(definition); }
namespace openjdkjvmti {
// Friendship only exposes the existing typed accessors to this temporary bridge;
// it adds no fields and changes no ART object layout.
struct FieldDeclarationBridge {
  static void Failure(void* definition, int error, const char* message) {
    static_cast<Redefiner::ClassRedefinition*>(definition)->RecordFailure(
        static_cast<jvmtiError>(error), message);
  }
  static bool Read(void* definition,
      artdcevm::android13::VisitFieldDeclaration visit, void* context) {
    auto* redef = static_cast<Redefiner::ClassRedefinition*>(definition);
    art::Thread* self = art::Thread::Current();
    art::StackHandleScope<1> hs(self);
    auto klass = hs.NewHandle(redef->GetMirrorClass());
    const auto& replacement = redef->GetDexFile();
    if (replacement.NumClassDefs() != 1) {
      redef->RecordFailure(JVMTI_ERROR_ILLEGAL_ARGUMENT, "Expected one replacement class");
      return false;
    }
    const auto& current = klass->GetDexFile();
    art::ClassAccessor old_accessor(current, *klass->GetClassDef());
    art::ClassAccessor new_accessor(replacement, replacement.GetClassDef(0));
    for (const auto& field : old_accessor.GetFields()) {
      const auto& id = current.GetFieldId(field.GetIndex());
      visit(context, false, current.StringDataByIdx(id.name_idx_),
            current.StringByTypeIdx(id.type_idx_), field.GetAccessFlags());
    }
    for (const auto& field : new_accessor.GetFields()) {
      const auto& id = replacement.GetFieldId(field.GetIndex());
      if (!field.IsStatic()) redef->RecordHasVirtualMembers();
      visit(context, true, replacement.StringDataByIdx(id.name_idx_),
            replacement.StringByTypeIdx(id.type_idx_), field.GetAccessFlags());
    }
    return true;
  }
};
}
extern "C" void ArtDcevmSourceFieldValidationFailure(void* definition, int error, const char* message) {
  openjdkjvmti::FieldDeclarationBridge::Failure(definition, error, message);
}
extern "C" void ArtDcevmSourceMarkFinalFieldGuard() {
  artdcevm::android13::source::MarkFinalFieldAddition();
}
extern "C" bool ArtDcevmSourceReadFieldDeclarations(void* definition,
    artdcevm::android13::VisitFieldDeclaration visit, void* context) {
  return openjdkjvmti::FieldDeclarationBridge::Read(definition, visit, context);
}

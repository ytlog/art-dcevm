#include "ti_redefine.h"
#include "dex/dex_file.h"
#include "retired_fields.h"
#include "platform/android13/field_validation_abi.h"
#include "platform/android13/redefinition_art_abi.h"

namespace {
artdcevm::android13::FieldValidation validate = nullptr;
// Build-time verification against the locked source declarations. This temporary
// bridge never interprets fields or DEX content on behalf of the enhancement.
struct DexFileLayoutProbe : art::DexFile {
  static void Verify() {
    using Prefix = artdcevm::android13::redefinition_abi::DexFilePrefix;
    static_assert(offsetof(DexFileLayoutProbe, begin_) == offsetof(Prefix, begin));
    static_assert(offsetof(DexFileLayoutProbe, size_) == offsetof(Prefix, size));
  }
};
}
extern "C" void ArtDcevmSourceConfigureFieldValidation(
    artdcevm::android13::FieldValidation callback) { validate = callback; }
extern "C" bool ArtDcevmSourceCheckFields(void* definition) { return validate(definition); }
namespace openjdkjvmti {
struct FieldDeclarationBridge {
  static void Verify() {
    using Original = Redefiner::ClassRedefinition;
    using Fixed = artdcevm::android13::redefinition_abi::ClassRedefinition;
    static_assert(sizeof(Original) == sizeof(Fixed));
    static_assert(offsetof(Original, driver_) == offsetof(Fixed, driver));
    static_assert(offsetof(Original, klass_) == offsetof(Fixed, klass));
    static_assert(offsetof(Original, dex_file_) == offsetof(Fixed, dex_file));
    static_assert(offsetof(Original, class_sig_) == offsetof(Fixed, class_signature));
    static_assert(offsetof(Original, original_dex_file_) == offsetof(Fixed, original_dex_file));
    static_assert(offsetof(Original, added_fields_) == offsetof(Fixed, added_fields));
    static_assert(offsetof(Original, added_methods_) == offsetof(Fixed, added_methods));
    static_assert(offsetof(Original, has_virtuals_) == offsetof(Fixed, has_virtuals));
    static_assert(offsetof(Original, lock_acquired_) == offsetof(Fixed, lock_acquired));
  }
  static void Failure(void* definition, int error, const char* message) {
    static_cast<Redefiner::ClassRedefinition*>(definition)->RecordFailure(
        static_cast<jvmtiError>(error), message);
  }
};
}
extern "C" int ArtDcevmSourceProbeFieldValidationLayout() {
  openjdkjvmti::FieldDeclarationBridge::Verify();
  DexFileLayoutProbe::Verify();
  return 0;
}
extern "C" void ArtDcevmSourceFieldValidationFailure(void* definition, int error, const char* message) {
  openjdkjvmti::FieldDeclarationBridge::Failure(definition, error, message);
}
extern "C" void ArtDcevmSourceMarkFinalFieldGuard() {
  artdcevm::android13::source::MarkFinalFieldAddition();
}

#include "ti_redefine.h"
#include "dex/dex_file.h"
#include "platform/android13/field_validation_abi.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/platform_string_abi.h"
#include <cstring>
#include <new>

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
  static int ProbeFailure(artdcevm::android13::FieldValidationFailure record) {
    using Text = artdcevm::android13::BorrowedPlatformString;
    using Fixed = artdcevm::android13::redefinition_abi::ClassRedefinition;
    static_assert(sizeof(std::string) == sizeof(Text));
    static_assert(alignof(std::string) == alignof(Text));
#ifdef _LIBCPP_ABI_ALTERNATE_STRING_LAYOUT
#error Platform error strings require the Android 13 ABI v1 string layout
#endif
    std::string native(64, 'x');
    Text representation("");
    std::memcpy(&representation, &native, sizeof(representation));
    if ((representation.capacity_tag & 1) != 1 || representation.length != native.size() ||
        representation.data != native.c_str()) return 1;
    // Exercise the named device recorder against a local, empty transaction.
    // No real class is modified; both short and long class signatures and error
    // strings must produce the same error text and result as the source contract.
    for (const char* signature : {"LP;", "Lartdcevm/NamedFailureLayoutCalibration;"}) {
      std::string class_name(signature);
      for (const char* message : {"", "short", "Long error text exceeding the platform small-string buffer"}) {
        std::string error;
        // This empty calibration driver owns no redefinitions or native memory.
        // Do not instantiate the deleted copied ClassRedefinition destructor.
        alignas(Redefiner) unsigned char driver_storage[sizeof(Redefiner)];
        auto& driver = *new (driver_storage) Redefiner(
            nullptr, nullptr, nullptr, RedefinitionType::kStructural, &error);
        Fixed definition{};
        definition.driver = &driver;
        std::memcpy(definition.class_signature, &class_name, sizeof(class_name));
        record(&definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED, message);
        if (driver.result_ != JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED ||
            error != std::string("Unable to perform redefinition of '") + signature + "': " + message)
          return 2;
      }
    }
    return 0;
  }
};
}
extern "C" int ArtDcevmSourceProbeFieldValidationLayout(
    artdcevm::android13::FieldValidationFailure record) {
  openjdkjvmti::FieldDeclarationBridge::Verify();
  DexFileLayoutProbe::Verify();
  return openjdkjvmti::FieldDeclarationBridge::ProbeFailure(record);
}

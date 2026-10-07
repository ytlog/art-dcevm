#include "platform/android13/field_validation.h"
#include "platform/android13/field_validation_abi.h"
#include "platform/android13/field_evolution_policy.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"
#include <dlfcn.h>
#include <new>
#include <cstring>
#include <limits>
#include <algorithm>
#include <jvmti.h>

namespace artdcevm::android13 {
namespace {
FieldValidationFailure record_failure = nullptr;
MarkFinalFieldGuard mark_final = nullptr;
using RecordNewField = void (*)(void*);
RecordNewField record_new_field = nullptr;

}

bool CheckEvolutionFields(void* definition) {
  try {
    auto* env = ValidationHooks::TransactionEnvironment();
    if (!env) {
      record_failure(definition, JVMTI_ERROR_NOT_AVAILABLE, "No active evolution environment");
      return false;
    }
    redefinition_abi::ClassRedefinition redef{};
    std::memcpy(&redef, definition, sizeof(redef));
    if (!redef.klass || !redef.dex_file) {
      record_failure(definition, JVMTI_ERROR_INTERNAL, "Missing class or replacement DEX");
      return false;
    }
    redefinition_abi::DexFilePrefix dex{};
    std::memcpy(&dex, redef.dex_file, sizeof(dex));
    if (!dex.begin || dex.size < 0x70 || dex.size > static_cast<size_t>(std::numeric_limits<jint>::max())) {
      record_failure(definition, JVMTI_ERROR_INTERNAL, "Invalid replacement DEX range");
      return false;
    }
    evolution::ClassSchema replacement;
    std::string error;
    std::vector<unsigned char> bytes(dex.begin, dex.begin + dex.size);
    if (!evolution::ReadDexSchema(bytes, &replacement, &error)) {
      record_failure(definition, JVMTI_ERROR_INVALID_CLASS_FORMAT, error.c_str());
      return false;
    }
    std::vector<evolution::Member> old_fields;
    int result = ReadLiveFields(env, redef.klass, &old_fields, &error);
    if (result != JVMTI_ERROR_NONE) {
      record_failure(definition, result, error.c_str());
      return false;
    }
    if (std::any_of(replacement.fields.begin(), replacement.fields.end(),
                    [](const auto& field) { return !field.IsStatic(); })) {
      const bool has_virtuals = true;
      std::memcpy(static_cast<unsigned char*>(definition) +
                      offsetof(redefinition_abi::ClassRedefinition, has_virtuals),
                  &has_virtuals, sizeof(has_virtuals));
    }
    auto decision = CheckFieldEvolution(old_fields, replacement.fields);
    if (!decision.accepted) {
      record_failure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                     decision.error.c_str());
      return false;
    }
    if (decision.final_guard) mark_final();
    if (decision.structural) record_new_field(definition);
    return true;
  } catch (const std::bad_alloc&) {
    record_failure(definition, JVMTI_ERROR_OUT_OF_MEMORY, "Unable to collect field declarations");
    return false;
  }
}

bool ConfigureEvolutionFieldValidation(HookEngine& hooks, void* implementation, std::string* error) {
  auto probe = reinterpret_cast<ProbeFieldValidationLayout>(
      dlsym(implementation, "ArtDcevmSourceProbeFieldValidationLayout"));
  auto failure = reinterpret_cast<FieldValidationFailure>(
      dlsym(implementation, "ArtDcevmSourceFieldValidationFailure"));
  auto final_guard = reinterpret_cast<MarkFinalFieldGuard>(
      dlsym(implementation, "ArtDcevmSourceMarkFinalFieldGuard"));
  auto configure = reinterpret_cast<ConfigureFieldValidation>(
      dlsym(implementation, "ArtDcevmSourceConfigureFieldValidation"));
  auto record = reinterpret_cast<RecordNewField>(
      hooks.FindSymbol("libopenjdkjvmti.so", cas::kRecordNewFieldAdded));
  if (!probe || !failure || !final_guard || !configure || !record) {
    *error = "Missing field layout probe, error bridge or named structural field marker";
    return false;
  }
  if (probe() != 0) {
    *error = "Fixed field-validation ART declarations did not match the source profile";
    return false;
  }
  record_failure = failure;
  mark_final = final_guard;
  record_new_field = record;
  configure(CheckEvolutionFields);
  return true;
}
}

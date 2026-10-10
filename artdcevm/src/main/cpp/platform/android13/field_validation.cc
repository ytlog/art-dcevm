#include "platform/android13/field_validation.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/failure_calibration.h"
#include "platform/common/native_schema_policy.h"
#include "platform/common/modern_final_field_guard.h"
#include "platform/android13/redefinition_input.h"
#include "platform/common/schema_snapshot.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13/source_symbols.h"
#include <new>
#include <cstring>
#include <algorithm>
#include <jvmti.h>

namespace artdcevm::android13 {
namespace {
using RecordNewField = void (*)(void*);
RecordNewField record_new_field = nullptr;

}

bool CheckEvolutionFields(void* definition) {
  try {
    auto* env = ValidationHooks::TransactionEnvironment();
    if (!env) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_NOT_AVAILABLE, "No active evolution environment");
      return false;
    }
    redefinition_abi::ClassRedefinition redef{};
    evolution::ClassSchema replacement;
    std::string error;
    int result = ReadRedefinitionInput(definition, &redef, &replacement, &error);
    if (result) { RecordRedefinitionFailure(definition, result, error.c_str()); return false; }
    std::vector<evolution::Member> old_fields;
    result = live_schema::ReadLiveFields(env, redef.klass, &old_fields, &error);
    if (result != JVMTI_ERROR_NONE) {
      RecordRedefinitionFailure(definition, result, error.c_str());
      return false;
    }
    if (std::any_of(replacement.fields.begin(), replacement.fields.end(),
                    [](const auto& field) { return !field.IsStatic(); })) {
      const bool has_virtuals = true;
      std::memcpy(static_cast<unsigned char*>(definition) +
                      offsetof(redefinition_abi::ClassRedefinition, has_virtuals),
                  &has_virtuals, sizeof(has_virtuals));
    }
    auto decision = native_schema_policy::CheckFieldEvolution(old_fields, replacement.fields);
    if (!decision.accepted) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                     decision.error.c_str());
      return false;
    }
    if (decision.final_guard) final_field_guard::MarkFinalFieldGuard();
    if (decision.structural) record_new_field(definition);
    return true;
  } catch (const std::bad_alloc&) {
    RecordRedefinitionFailure(definition, JVMTI_ERROR_OUT_OF_MEMORY, "Unable to collect field declarations");
    return false;
  }
}

bool ConfigureEvolutionFieldValidation(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!CalibrateRedefinitionFailures(hooks, profile, error)) return false;
  auto record = reinterpret_cast<RecordNewField>(
      hooks.FindSymbol("libopenjdkjvmti.so", ::artdcevm::symbols::android13::kRecordNewFieldAdded));
  if (!record) {
    *error = "Missing named structural field marker";
    return false;
  }
  record_new_field = record;
  return true;
}
}

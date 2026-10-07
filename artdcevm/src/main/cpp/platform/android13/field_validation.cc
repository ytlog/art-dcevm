#include "platform/android13/field_validation.h"
#include "platform/android13/field_validation_abi.h"
#include "platform/android13/field_evolution_policy.h"
#include "platform/symbols/android13.h"
#include <dlfcn.h>
#include <new>
#include <jvmti.h>

namespace artdcevm::android13 {
namespace {
ReadFieldDeclarations read_fields = nullptr;
FieldValidationFailure record_failure = nullptr;
MarkFinalFieldGuard mark_final = nullptr;
using RecordNewField = void (*)(void*);
RecordNewField record_new_field = nullptr;
struct Declarations {
  std::vector<evolution::Member> old_fields, new_fields;
  bool out_of_memory = false;
};
void Collect(void* context, bool replacement, const char* name, const char* type, uint32_t flags) {
  auto& fields = *static_cast<Declarations*>(context);
  if (fields.out_of_memory) return;
  // Do not unwind through the transitional AOSP module built without exceptions.
  try {
    (replacement ? fields.new_fields : fields.old_fields).push_back({name, type, flags, UINT32_MAX});
  } catch (const std::bad_alloc&) {
    fields.out_of_memory = true;
  }
}
}

bool CheckEvolutionFields(void* definition) {
  try {
    Declarations fields;
    if (!read_fields(definition, Collect, &fields)) return false;
    if (fields.out_of_memory) throw std::bad_alloc();
    auto decision = CheckFieldEvolution(fields.old_fields, fields.new_fields);
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
  auto read = reinterpret_cast<ReadFieldDeclarations>(
      dlsym(implementation, "ArtDcevmSourceReadFieldDeclarations"));
  auto failure = reinterpret_cast<FieldValidationFailure>(
      dlsym(implementation, "ArtDcevmSourceFieldValidationFailure"));
  auto final_guard = reinterpret_cast<MarkFinalFieldGuard>(
      dlsym(implementation, "ArtDcevmSourceMarkFinalFieldGuard"));
  auto configure = reinterpret_cast<ConfigureFieldValidation>(
      dlsym(implementation, "ArtDcevmSourceConfigureFieldValidation"));
  auto record = reinterpret_cast<RecordNewField>(
      hooks.FindSymbol("libopenjdkjvmti.so", cas::kRecordNewFieldAdded));
  if (!read || !failure || !final_guard || !configure || !record) {
    *error = "Missing field declaration bridge or named structural field marker";
    return false;
  }
  read_fields = read;
  record_failure = failure;
  mark_final = final_guard;
  record_new_field = record;
  configure(CheckEvolutionFields);
  return true;
}
}

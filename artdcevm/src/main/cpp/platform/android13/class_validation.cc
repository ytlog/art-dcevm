#include "platform/android13/class_validation.h"
#include "platform/android13/class_validation_abi.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "platform/android13/redefinition_input.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/validation_metadata.h"
#include <cstring>
#include <new>
namespace artdcevm::android13 {
namespace {
using RecordNewMethod = void (*)(void*);
RecordNewMethod record_new_method = nullptr;
}
bool CheckEvolutionClass(void* definition) {
  try {
    auto* env = ValidationHooks::TransactionEnvironment();
    auto* jni = ValidationHooks::TransactionJni();
    if (!env || !jni) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL, "Missing class validation transaction");
      return false;
    }
    redefinition_abi::ClassRedefinition redef{};
    evolution::ClassSchema replacement;
    std::string error;
    int result = ReadRedefinitionInput(definition, &redef, &replacement, &error);
    if (result) { RecordRedefinitionFailure(definition, result, error.c_str()); return false; }
    evolution::ClassSchema current;
    result = ReadLiveClassMetadata(env, jni, redef.klass, &current, &error);
    if (result) { RecordRedefinitionFailure(definition, result, error.c_str()); return false; }
    if (!redef.lock_acquired) {
      error = "Failed to lock class object '" + current.descriptor + "'";
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL, error.c_str());
      return false;
    }
    ValidationMetadata old_metadata{};
    if (!ReadValidationMetadata(definition, &old_metadata)) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL, "Missing original class metadata");
      return false;
    }
    const uint32_t old_flags = old_metadata.access_flags & kValidationClassFlags;
    const uint32_t new_flags = replacement.access &
        ((replacement.access & 0x200) ? kValidationInterfaceFlags : kValidationClassFlags);
    const auto action = CheckClassModifierEvolution(old_flags, new_flags, true);
    if (action == ModifierEvolutionAction::kReject) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                               "Cannot change modifiers of class by redefinition");
      return false;
    }
    if (action == ModifierEvolutionAction::kCheckLoadedSubclass && !CheckLoadedSubclassGuard(definition)) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                               "A loaded subclass prevents making this class final");
      return false;
    }
    if (action != ModifierEvolutionAction::kUnchanged) record_new_method(definition);
    if (current.descriptor != replacement.descriptor) {
      error = "expected file to contain class called '" + current.descriptor +
          "' but found '" + replacement.descriptor + "'!";
      RecordRedefinitionFailure(definition, JVMTI_ERROR_NAMES_DONT_MATCH, error.c_str());
      return false;
    }
    // The owned hierarchy validator roots every class across named ART resolution.
    bool changed = false;
    result = CheckEvolutionHierarchy(definition, current, replacement, &changed, &error);
    if (result) {
      RecordRedefinitionFailure(definition, result, error.c_str());
      return false;
    }
    const bool requested_change = current.superclass != replacement.superclass ||
        current.interfaces != replacement.interfaces;
    if (requested_change != changed) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL,
                               "Hierarchy validation disagrees with class declarations");
      return false;
    }
    if (changed) record_new_method(definition);
    return true;
  } catch (const std::bad_alloc&) {
    RecordRedefinitionFailure(definition, JVMTI_ERROR_OUT_OF_MEMORY, "Unable to collect class declarations");
    return false;
  }
}
bool ConfigureEvolutionClassValidation(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  auto record = reinterpret_cast<RecordNewMethod>(hooks.FindSymbol("libopenjdkjvmti.so",
      "_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"));
  if (!record) {
    *error = "Missing class validation named structural marker";
    return false;
  }
  record_new_method = record;
  return true;
}
}

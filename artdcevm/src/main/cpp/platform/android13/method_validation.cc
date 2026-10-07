#include "platform/android13/method_validation.h"
#include "platform/android13/method_validation_abi.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "platform/android13/redefinition_input.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/android13/validation_hooks.h"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <new>

namespace artdcevm::android13 {
namespace {

ReadOldDexMetadata read_old_metadata = nullptr;
MethodDependentGuard dependents = nullptr;
using RecordNewMethod = void (*)(void*);
RecordNewMethod record_new_method = nullptr;
}
bool CheckEvolutionMethods(void* definition) {
  try {
    auto* env = ValidationHooks::TransactionEnvironment();
    if (!env) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL, "Missing method validation transaction");
      return false;
    }
    redefinition_abi::ClassRedefinition redef{};
    evolution::ClassSchema replacement;
    std::string error;
    int result = ReadRedefinitionInput(definition, &redef, &replacement, &error);
    if (result) { RecordRedefinitionFailure(definition, result, error.c_str()); return false; }
    evolution::MappedDexMetadata old_mapping{};
    evolution::ClassSchema old_declarations;
    if (!read_old_metadata(definition, &old_mapping) ||
        !evolution::ReadMappedDexDeclarations(old_mapping, &old_declarations, &error)) {
      RecordRedefinitionFailure(definition, JVMTI_ERROR_INTERNAL,
          error.empty() ? "Missing original DEX mapping" : error.c_str());
      return false;
    }
    const auto& old_methods = old_declarations.methods;
    for (const auto& method : replacement.methods) {
      // DEX direct methods are static, private, or constructors. Class data has
      // already been checked by the parser and ART's incoming DEX verifier.
      const bool direct = (method.access & (0x0008 | 0x0002)) != 0 ||
          method.name == "<init>" || method.name == "<clinit>";
      if (!direct) {
        const bool has_virtuals = true;
        std::memcpy(static_cast<unsigned char*>(definition) +
            offsetof(redefinition_abi::ClassRedefinition, has_virtuals),
            &has_virtuals, sizeof(has_virtuals));
      }
      auto old = std::find_if(old_methods.begin(), old_methods.end(), [&](const auto& candidate) {
        return candidate.name == method.name && candidate.descriptor == method.descriptor;
      });
      if (old == old_methods.end()) continue;
      auto action = CheckMethodModifierEvolution(old->access, method.access, true, direct);
      if (action == ModifierEvolutionAction::kReject) {
        const std::string message = "method " + method.name + method.descriptor + " had different access flags";
        RecordRedefinitionFailure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED, message.c_str());
        return false;
      }
      if ((action == ModifierEvolutionAction::kCheckFinalOverride ||
           action == ModifierEvolutionAction::kCheckAbstractDependents) &&
          !dependents(definition, method.name.c_str(), method.descriptor.c_str(),
                      old->access, static_cast<int>(action))) {
        const std::string message = std::string(action == ModifierEvolutionAction::kCheckFinalOverride
            ? "Loaded subclass overrides " : "Concrete subclass inherits ") + method.name + method.descriptor;
        RecordRedefinitionFailure(definition, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                                   message.c_str());
        return false;
      }
    }
    // Every enhancement transaction recreates the class, including unchanged
    // declarations with live inlined frames. ART owns the actual structural swap.
    record_new_method(definition);
    return true;
  } catch (const std::bad_alloc&) {
    RecordRedefinitionFailure(definition, JVMTI_ERROR_OUT_OF_MEMORY, "Unable to collect method declarations");
    return false;
  }
}
bool ConfigureEvolutionMethodValidation(HookEngine& hooks, void* implementation, std::string* error) {
  auto read = reinterpret_cast<ReadOldDexMetadata>(dlsym(implementation, "ArtDcevmSourceReadOldDexMetadata"));
  auto guard = reinterpret_cast<MethodDependentGuard>(dlsym(implementation, "ArtDcevmSourceMethodDependentGuard"));
  auto configure = reinterpret_cast<ConfigureMethodValidation>(dlsym(implementation, "ArtDcevmSourceConfigureMethodValidation"));
  auto record = reinterpret_cast<RecordNewMethod>(hooks.FindSymbol("libopenjdkjvmti.so",
      "_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"));
  if (!read || !guard || !configure || !record) {
    *error = "Missing method declaration/loaded-subclass adapter or named structural method marker";
    return false;
  }
  read_old_metadata = read; dependents = guard; record_new_method = record;
  configure(CheckEvolutionMethods);
  return true;
}
}

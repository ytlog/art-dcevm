#pragma once
#include "evolution/class_schema.h"
#include <jvmti.h>
#include <set>
#include <string>
#include <utility>

// Phase 2 validation rules (docs/STRUCTURAL_REDEFINE_ENGINE.md). Pure
// functions over declarations; no ART or JNI access.
namespace artdcevm::structural_policy {

using MemberKey = std::pair<std::string, std::string>;  // name, descriptor

struct SubclassMethods {
  // Non-private instance methods declared by loaded subclasses.
  std::set<MemberKey> visible;
  // The subset declared in the target's package by its class loader.
  std::set<MemberKey> same_runtime_package;
};

struct Decision {
  jvmtiError error = JVMTI_ERROR_NONE;
  std::string reason;
  // Any field/method addition or modifier change: ART must take its
  // structural path so the new Class carries the new declarations.
  bool fields_structural = false;
  bool methods_structural = false;
};

// Raw DEX access flags mapped to what JVMTI Get*Modifiers reports
// (openjdkjvmti ti_class.cc, ti_field.cc, ti_method.cc).
uint32_t ClassModifiers(uint32_t dex_access);
uint32_t FieldModifiers(uint32_t dex_access);
uint32_t MethodModifiers(uint32_t dex_access);

// `live` carries JVMTI modifiers, `incoming` raw DEX flags. `subclass_methods`
// lists possible overrides declared by every loaded subclass of the target.
Decision Evaluate(const evolution::ClassSchema& live, const evolution::ClassSchema& incoming,
                  const SubclassMethods& subclass_methods);

}  // namespace artdcevm::structural_policy

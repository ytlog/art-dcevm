#pragma once
#include "evolution/class_schema.h"
#include <jvmti.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Phase 2 and method-only Phase 3 rules (docs/STRUCTURAL_REDEFINE_ENGINE.md). Pure
// functions over declarations; no ART or JNI access.
namespace artdcevm::structural_policy {

using MemberKey = std::pair<std::string, std::string>;  // name, descriptor

// Loaded classes related to a redefinition target, read through public JVMTI.
struct LoadedHierarchy {
  // Non-private instance methods declared by loaded subclasses.
  std::set<MemberKey> visible;
  // The subset declared in the target's package by its class loader.
  std::set<MemberKey> same_runtime_package;
  // Interface methods of a loaded subclass that the subclass does not declare.
  // ART may link them as copied default/miranda members of that subclass.
  std::set<MemberKey> interface_copies;
  // Public instance methods declared by the target's superclasses.
  std::set<MemberKey> inherited_public;
};

struct Decision {
  jvmtiError error = JVMTI_ERROR_NONE;
  std::string reason;
  // Any field/method addition, retirement or modifier change: ART must take its
  // structural path so the new Class carries the new declarations.
  bool fields_structural = false;
  bool methods_structural = false;
  // Old declarations absent from the new class.
  std::vector<evolution::Member> retired_methods;
  // Old fields without a new field of the same name, type and storage.
  std::vector<evolution::Member> retired_fields;
};

// Raw DEX access flags mapped to what JVMTI Get*Modifiers reports
// (openjdkjvmti ti_class.cc, ti_field.cc, ti_method.cc).
uint32_t ClassModifiers(uint32_t dex_access);
uint32_t FieldModifiers(uint32_t dex_access);
uint32_t MethodModifiers(uint32_t dex_access);

// `live` carries JVMTI modifiers, `incoming` raw DEX flags. `owns_member_mapping`
// means the engine replaces ART's member mapping for this runtime: it can retire
// methods and remap copied members that disappear from re-linked subclasses.
// `retires_fields` additionally allows removing a field or changing its type or
// storage; the old field retires and the new one starts from its default value.
Decision Evaluate(const evolution::ClassSchema& live, const evolution::ClassSchema& incoming,
                  const LoadedHierarchy& hierarchy, bool owns_member_mapping = false,
                  bool retires_fields = false);

}  // namespace artdcevm::structural_policy

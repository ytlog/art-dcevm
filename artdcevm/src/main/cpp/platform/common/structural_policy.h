#pragma once
#include "evolution/class_schema.h"
#include <jvmti.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Structural redefinition rules (docs/STRUCTURAL_REDEFINE_ENGINE.md). Pure
// functions over declarations; no ART or JNI access.
namespace artdcevm::structural_policy {

using MemberKey = std::pair<std::string, std::string>;  // name, descriptor

// An interface the new definition adds, resolved through the target's class
// loader before the transaction.
struct ResolvedType {
  // Java bits of the runtime Class flags: its ClassDef flags, not the InnerClass
  // flags JVMTI reports, as ART's Class::CanAccess reads them. Empty when no
  // verified class layout could read them.
  std::optional<uint32_t> access;
  // Same package name and defining class loader as the target.
  bool same_runtime_package = false;
};

// A new superclass, resolved through the target's class loader before the
// transaction. Only widening is supported: it must extend the old superclass.
struct NewSuperclass {
  // As ResolvedType::access: runtime ClassDef flags, empty without a verified layout.
  std::optional<uint32_t> access;
  bool same_runtime_package = false;
  bool extends_old_superclass = false;
  bool extends_target = false;  // Circular.
  // Final instance methods it and its superclasses below the old superclass
  // declare, as far as the target's package may override them.
  std::set<MemberKey> final_methods;
  // Another class of the batch is a subclass or superclass of the target, or of the new superclass.
  bool related_in_batch = false;
};

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
  // Whether any loaded, prepared class extends the target.
  bool loaded_subclasses = false;
  // Added interfaces by descriptor; one absent here could not be resolved.
  std::map<std::string, ResolvedType> added_interfaces;
  // Present when the new superclass differs and could be resolved.
  std::optional<NewSuperclass> new_superclass;
};

// What the engine can enforce for this transaction beyond ART's own rules.
struct Abilities {
  // The engine replaces ART's member mapping: methods can retire and copied
  // members that disappear from re-linked subclasses are remapped.
  bool member_mapping = false;
  // Fields can retire (requires member_mapping): removal, type or storage change.
  bool field_retirement = false;
  // Instances of the target can be inspected before ART's no-failure point.
  bool instance_check = false;
  // ART's verifier can be shown the new class under a new superclass.
  bool superclass_change = false;
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
  // The class becomes abstract: the transaction must fail if instances exist.
  bool requires_no_instances = false;
  // ART must link the new class under the new superclass.
  bool superclass_changed = false;
};

// Raw DEX access flags mapped to what JVMTI Get*Modifiers reports
// (openjdkjvmti ti_class.cc, ti_field.cc, ti_method.cc).
uint32_t ClassModifiers(uint32_t dex_access);
uint32_t FieldModifiers(uint32_t dex_access);
uint32_t MethodModifiers(uint32_t dex_access);

// `live` carries JVMTI modifiers, `incoming` raw DEX flags. A retired field's
// replacement starts from its default value; a retired method throws on new access.
Decision Evaluate(const evolution::ClassSchema& live, const evolution::ClassSchema& incoming,
                  const LoadedHierarchy& hierarchy, const Abilities& abilities = {});

}  // namespace artdcevm::structural_policy

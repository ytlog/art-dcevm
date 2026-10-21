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
  // A subclass of the target in the batch changes its own superclass too.
  bool moving_subclass_in_batch = false;
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
  // An added interface that is, or extends, another target of the batch.
  std::optional<std::string> added_interface_in_batch;
  // Present when the new superclass differs and could be resolved.
  std::optional<NewSuperclass> new_superclass;
  // Interface targets: virtual methods the target inherits from its
  // superinterfaces. Object public methods are in inherited_public. An added direct method with one of
  // these identities would be resolved by verified invoke-interface callers.
  std::set<MemberKey> inherited_interface;
  // Interface targets: package-private methods declared by loaded subtypes, or
  // by superclasses of loaded implementations in another runtime package, each
  // with the first declaring class. ART resolves a subtype's call that names
  // such a method to an added default copied into the subtype, where JBR keeps
  // the declaration; other static or non-public methods resolve as on JBR.
  std::map<MemberKey, std::string> subtype_shadows;
  // Interface targets: the virtual methods that added parent interfaces bring,
  // read from each parent and those of its superinterfaces the target does not
  // already inherit. Keyed by declaring interface descriptor.
  struct ParentMethod {
    MemberKey key;
    bool is_abstract;
  };
  std::map<std::string, std::vector<ParentMethod>> parent_methods;
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
  // Interface targets may add abstract, default, static and private methods and retire
  // static and private ones (requires member_mapping): the hierarchy carries
  // inherited_interface and subtype_shadows.
  bool interface_members = false;
  // A class method may change its invoke kind (static <-> instance, virtual ->
  // private). The engine checks loaded callers and either protects their
  // resolution/compilation or requires them in the batch (requires member_mapping).
  bool kind_changes = false;
  // An added interface the batch also rebuilds can link as its replacement:
  // the engine allocates that rebuild first.
  bool batch_interfaces = false;
  // An ordinary class may stop implementing interfaces: the engine seals the
  // interface dispatch of its replacement classes so that verified callers
  // still treating them as the removed interface throw
  // IncompatibleClassChangeError (requires member_mapping).
  bool interface_removal = false;
  // Pre-publication linking preserves JBR non-public class-method selection.
  bool interface_selection = false;
  // Old symbolic interface owners are re-resolved after a committed hierarchy change.
  bool interface_symbols = false;
  // Kind changes use a new ART Class, verified member mapping and protected
  // old virtual/interface callers. Class -> interface additionally requires
  // no exact instances; changing the interface bit requires no loaded subtypes.
  bool class_kinds = false;
  // Canonical array lookup and register-type component redirection are available.
  bool self_array_verification = false;
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
  // Accepted decisions only: disabling mapping and all its derived abilities
  // (but keeping instance_check) would reject this evolution.
  bool requires_member_mapping = false;
  // ART must link the new class under the new superclass.
  bool superclass_changed = false;
  // The class stops implementing some interface: seal its replacements' dispatch.
  bool interfaces_removed = false;
  // The interface bit changes the meaning of a symbolic invocation even if
  // the method's own flags and signature stay identical.
  bool interface_kind_changed = false;
  // Must run before ART attempts to allocate any replacement instance.
  bool requires_instance_precheck = false;
  // Enum marker only, with identical hierarchy and member declarations: ART
  // verifies all incoming code using the existing self/array identity graph.
  bool published_verification = false;
  // Methods whose old invoke kind needs caller protection or batch checks,
  // with the old kind's modifiers. The old declarations retire.
  std::vector<std::pair<MemberKey, uint32_t>> kind_changes;
};

// Methods of the target that earlier transactions retired, with their JVMTI
// modifiers. Verified callers may still invoke them as that kind.
using RetiredMethods = std::map<MemberKey, uint32_t>;

// `live` carries JVMTI modifiers, `incoming` raw DEX flags. A retired field's
// replacement starts from its default value; a retired method throws on new access.
Decision Evaluate(const evolution::ClassSchema& live, const evolution::ClassSchema& incoming,
                  const LoadedHierarchy& hierarchy, const Abilities& abilities = {},
                  const RetiredMethods& retired = {});

}  // namespace artdcevm::structural_policy

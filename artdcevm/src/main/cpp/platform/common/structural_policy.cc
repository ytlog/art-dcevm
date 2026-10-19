#include "platform/common/structural_policy.h"
#include "evolution/modifiers.h"
#include <map>
#include <set>
#include <tuple>

namespace artdcevm::structural_policy {
namespace {
using evolution::ClassSchema;
using evolution::Member;
using evolution::ClassModifiers;
using evolution::FieldModifiers;
using evolution::MethodModifiers;
namespace acc = evolution;

constexpr uint32_t kJavaFlags = 0xffff;
constexpr uint32_t kAccessLevel = acc::kAccPublic | acc::kAccPrivate | acc::kAccProtected;
constexpr uint32_t kClassDefinitionFlags = kJavaFlags & ~(kAccessLevel | acc::kAccStatic);
constexpr uint32_t kAccSuper = 0x0020;
constexpr uint32_t kAccSynthetic = 0x1000;
// Class modifiers a structural redefinition may change: the new Class takes
// its flags from the new ClassDef (and reflection from its InnerClass entry).
// Kind bits are admitted separately, with allocation and dispatch prerequisites.
constexpr uint32_t kClassAllowed = kAccessLevel | acc::kAccStatic | acc::kAccFinal | kAccSuper |
    acc::kAccAbstract | kAccSynthetic;
constexpr uint32_t kClassKinds = acc::kAccInterface | acc::kAccAnnotation | acc::kAccEnum;
constexpr uint32_t kFieldTransient = 0x0080;
constexpr uint32_t kFieldAllowed = kAccessLevel | acc::kAccFinal | acc::kAccVolatile | kFieldTransient | kAccSynthetic;
// Bridge, varargs, strict and synthetic only describe the method to reflection
// and linking; the structural path gives the new method its new flags.
constexpr uint32_t kMethodDescriptive = 0x0040 | 0x0080 | 0x0800 | kAccSynthetic;
constexpr uint32_t kMethodAllowed = kAccessLevel | acc::kAccFinal | acc::kAccSynchronized | kMethodDescriptive;

Decision Reject(jvmtiError error, std::string reason) {
  Decision decision;
  decision.error = error;
  decision.reason = std::move(reason);
  return decision;
}

std::string Describe(const Member& m) { return m.name + m.descriptor; }

bool Initializer(const std::string& name) { return name == "<init>" || name == "<clinit>"; }

bool SameMembers(const std::vector<Member>& before, const std::vector<Member>& after, bool methods) {
  std::map<MemberKey, uint32_t> old, next;
  for (const auto& m : before) old[{m.name, m.descriptor}] = methods ? MethodModifiers(m.access) : FieldModifiers(m.access);
  for (const auto& m : after) next[{m.name, m.descriptor}] = methods ? MethodModifiers(m.access) : FieldModifiers(m.access);
  return old == next;
}

// Static and private methods never occupy a vtable or iftable slot.
bool Virtual(uint32_t modifiers) { return !(modifiers & (acc::kAccStatic | acc::kAccPrivate)); }

// Verified callers skip kind checks: static/instance switches change the
// receiver convention, and virtual-to-private removes the dispatch slot.
// These transitions require caller protection before admission. Private-to-
// virtual keeps old own-class invoke-direct calls valid, so needs no caller
// scan. Constructors and initializers always retain their direct convention.
bool BreaksVerifiedCallers(const std::string& name, uint32_t before, uint32_t after) {
  if (Initializer(name)) return false;
  return ((before ^ after) & acc::kAccStatic) || (Virtual(before) && !Virtual(after));
}

// ART links a subclass interface slot to a public superclass method in
// preference to a default, and otherwise copies the default or a miranda into
// that subclass. A new or re-leveled target method can therefore remove a
// copied member that ART's own mapping must find in the re-linked subclass.
Decision CheckCopiedMembers(const ClassSchema& live, const ClassSchema& incoming,
                            const LoadedHierarchy& hierarchy) {
  std::map<MemberKey, uint32_t> old_methods;
  for (const auto& m : live.methods) old_methods[{m.name, m.descriptor}] = m.access;
  for (const auto& m : incoming.methods) {
    const uint32_t now = MethodModifiers(m.access);
    const MemberKey key{m.name, m.descriptor};
    if (!Virtual(now) || Initializer(m.name) || !hierarchy.interface_copies.count(key) ||
        hierarchy.inherited_public.count(key)) continue;
    const auto old = old_methods.find(key);
    if (old == old_methods.end())
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                    "added method may replace a copied interface method of a loaded subclass: " +
                        Describe(m));
    if (!Virtual(old->second) || ((old->second ^ now) & kAccessLevel))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "access change may replace a copied interface method of a loaded subclass: " +
                        Describe(m));
  }
  return {};
}
// ART links the new class's direct interfaces by looking each up in its class
// loader (ClassLinker::LookupResolvedType) and checks only that it is an
// interface; the access check of LoadSuperAndInterfaces is not on that path.
// Removing an interface narrows the type of live instances. Verified code may
// still treat them as the removed interface; an ordinary class may drop it
// only when the engine seals its replacements' interface dispatch, so such
// calls throw IncompatibleClassChangeError as on a JVM.
Decision CheckInterfaces(const ClassSchema& live, const ClassSchema& incoming,
                         const LoadedHierarchy& hierarchy, const Abilities& abilities) {
  const std::set<std::string> before(live.interfaces.begin(), live.interfaces.end());
  const std::set<std::string> after(incoming.interfaces.begin(), incoming.interfaces.end());
  if (after.size() != incoming.interfaces.size())
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "duplicate interface");
  Decision decision;
  for (const auto& old : live.interfaces) {
    if (after.count(old)) continue;
    // An interface target drops a parent for its whole implementation group,
    // which the interface engine re-links (interface_members).
    if (!abilities.member_mapping || !abilities.interface_removal ||
        ((live.access & acc::kAccInterface) && !abilities.interface_members))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                    "interface removed: " + old);
    decision.interfaces_removed = true;
  }
  // The new class links the added interface's published Class: one the batch
  // also replaces would leave its copied defaults and slots on the old version.
  if (hierarchy.added_interface_in_batch && !abilities.batch_interfaces)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "added interface is redefined in the same batch: " + *hierarchy.added_interface_in_batch);
  for (const auto& added : incoming.interfaces) {
    if (before.count(added)) continue;
    const auto resolved = hierarchy.added_interfaces.find(added);
    if (resolved == hierarchy.added_interfaces.end())
      return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "added interface cannot be resolved: " + added);
    if (!resolved->second.access)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                    "added interface cannot be checked without a verified class layout: " + added);
    const uint32_t access = *resolved->second.access;
    if (!(access & acc::kAccInterface))
      return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "added type is not an interface: " + added);
    if (!(access & acc::kAccPublic) && !resolved->second.same_runtime_package)
      return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "added interface is inaccessible: " + added);
  }
  return decision;
}
// AllocateNewClassObject links the new class under the superclass the engine
// supplies; LinkClass alone would fail with a link error for a final or
// inaccessible superclass. Narrowing is unsupported: verified code still
// treats instances as every old supertype.
Decision CheckSuperclass(const ClassSchema& live, const ClassSchema& incoming,
                         const LoadedHierarchy& hierarchy, const Abilities& abilities) {
  const std::string& name = incoming.superclass;
  if (!hierarchy.new_superclass)
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "new superclass cannot be resolved: " + name);
  const NewSuperclass& superclass = *hierarchy.new_superclass;
  if (!superclass.access)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "new superclass cannot be checked without a verified class layout: " + name);
  const uint32_t access = *superclass.access;
  if (access & acc::kAccInterface)
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "new superclass is an interface: " + name);
  if (access & acc::kAccFinal)
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "new superclass is final: " + name);
  if (!(access & acc::kAccPublic) && !superclass.same_runtime_package)
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "new superclass is inaccessible: " + name);
  if (superclass.extends_target)
    return Reject(JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION, "new superclass extends the class: " + name);
  if (!superclass.extends_old_superclass)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "new superclass does not extend " + live.superclass + " (narrowing unsupported)");
  if (!abilities.member_mapping || !abilities.superclass_change)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "superclass change requires the engine's member mapping and verifier redirect");
  if (superclass.moving_subclass_in_batch)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "superclass change shares the batch with a subclass that changes its superclass");
  for (const auto& m : incoming.methods)
    if (Virtual(MethodModifiers(m.access)) && !Initializer(m.name) &&
        superclass.final_methods.count({m.name, m.descriptor}))
      return Reject(JVMTI_ERROR_FAILS_VERIFICATION,
                    "method overrides a final method of the new superclass: " + Describe(m));
  for (const auto& key : hierarchy.visible)
    if (superclass.final_methods.count(key))
      return Reject(JVMTI_ERROR_FAILS_VERIFICATION,
                    "loaded subclass overrides a final method of the new superclass: " +
                        key.first + key.second);
  return {};
}
// Only new code invokes an added static or private interface method; as for a
// class, ART never runs an initializer added to an initialized interface. An added
// default fills a new iftable slot that ART links into every re-linked
// subtype. Missing abstract implementations fail at the call. `modifiers`
// is the JVMTI view.
Decision CheckInterfaceAddition(const MemberKey& key, uint32_t modifiers,
                                const LoadedHierarchy& hierarchy, bool supported, bool selection, bool symbols = false) {
  const std::string method = key.first + key.second;
  if (!supported)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "interface redefinition does not support added methods: " + method);
  if (modifiers & acc::kAccNative)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "interface redefinition does not support added native methods: " + method);
  if (!Virtual(modifiers)) {
    // Inherited interface symbols need cache invalidation and invocation
    // guards before a direct declaration can replace their resolution.
    if (hierarchy.inherited_public.count(key) || (hierarchy.inherited_interface.count(key) && !symbols))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                    "added static or private method hides an inherited interface method: " + method);
    return {};
  }
  if (!(modifiers & acc::kAccPublic))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "added interface instance method is neither public nor private: " + method);
  // As on JBR, an implementation without the new abstract method links a
  // miranda: calls to it throw AbstractMethodError.
  if (modifiers & acc::kAccAbstract) return {};
  const auto shadow = hierarchy.subtype_shadows.find(key);
  if (shadow != hierarchy.subtype_shadows.end() && !selection)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "added default method may change calls resolved by " + shadow->second + ": " + method);
  return {};
}
// An added parent interface brings its virtual methods, and those of its
// superinterfaces the target did not inherit, to every implementation: like
// added members. Direct declarations do not implement the parent method;
// symbolic lookup must keep the child declaration and parent dispatch distinct.
// A default must not shadow a subtype method without selection support.
Decision CheckInterfaceParents(const ClassSchema& incoming, const LoadedHierarchy& hierarchy, bool selection, bool symbols) {
  std::set<MemberKey> direct;
  for (const auto& m : incoming.methods) {
    const uint32_t now = MethodModifiers(m.access);
    if (!Virtual(now)) direct.insert({m.name, m.descriptor});
  }
  for (const auto& [declaring, methods] : hierarchy.parent_methods) {
    for (const auto& method : methods) {
      const std::string name = declaring + "." + method.key.first + method.key.second;
      if (direct.count(method.key) && !symbols)
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                      "a static or private method hides an added parent interface method: " + name);
      if (!method.is_abstract) {
        const auto shadow = hierarchy.subtype_shadows.find(method.key);
        if (shadow != hierarchy.subtype_shadows.end() && !selection)
          return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                        "added parent interface default may change calls resolved by " + shadow->second + ": " + name);
        continue;
      }
      // An abstract method an implementation lacks throws AbstractMethodError
      // when called, as on JBR.
    }
  }
  return {};
}
// A public interface instance method may switch between abstract and default.
// A new default is an added default for every implementation; a new abstract
// declaration throws AbstractMethodError when no implementation exists.
Decision CheckInterfaceKindChange(const MemberKey& key, uint32_t before, uint32_t after,
                                  const LoadedHierarchy& hierarchy, bool supported, bool selection) {
  const std::string method = key.first + key.second;
  // Descriptive bits use the common modifier path, including combinations
  // with abstract/default changes. They do not change interface selection.
  before &= ~kMethodDescriptive;
  after &= ~kMethodDescriptive;
  if (supported && before == after) return {};
  // Restoring a private instance declaration as public gives implementations
  // a default or miranda again. Reuse addition's visibility/shadow checks;
  // old invoke-direct self calls remain valid without a caller scan.
  if (supported && (before & acc::kAccPrivate) && !(before & acc::kAccStatic) && Virtual(after))
    return CheckInterfaceAddition(key, after, hierarchy, supported, selection);
  if (!supported || (before ^ after) != acc::kAccAbstract || !Virtual(before) || !(before & acc::kAccPublic))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                  "interface redefinition does not support method modifier changes: " + method);
  // An implementation that relied on the default links a miranda.
  if (after & acc::kAccAbstract) return {};
  const auto shadow = hierarchy.subtype_shadows.find(key);
  if (shadow != hierarchy.subtype_shadows.end() && !selection)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                  "interface method made default may change calls resolved by " + shadow->second + ": " + method);
  return {};
}
}  // namespace


Decision Evaluate(const ClassSchema& live, const ClassSchema& incoming,
                  const LoadedHierarchy& hierarchy, const Abilities& abilities,
                  const RetiredMethods& retired) {
  const bool interface_target = live.access & acc::kAccInterface;
  const bool incoming_interface = incoming.access & acc::kAccInterface;
  const bool interface_kind_changed = interface_target != incoming_interface;
  const bool owns_member_mapping = abilities.member_mapping;
  const bool retires_fields = abilities.member_mapping && abilities.field_retirement;
  const bool interface_members = abilities.member_mapping && abilities.interface_members;
  const bool superclass_changed = live.superclass != incoming.superclass;
  const bool parents_changed = live.interfaces != incoming.interfaces;
  // JBR accepts this without live objects, but ART additionally needs a new
  // self-array identity graph and libcore's enum constants cache does not
  // accept the uninitialized values array that JBR reports as null. Keeping
  // an existing Enum superclass does not introduce either mismatch.
  if (superclass_changed && (incoming.access & acc::kAccEnum))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "new Enum superclass requires self-array and uninitialized enum-constant semantics");
  if (interface_kind_changed && incoming.self_array_reference)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "class/interface conversion requires a replacement graph for self-array components");
  if (interface_target && !interface_kind_changed && (superclass_changed || (parents_changed && !interface_members)))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "interface redefinition does not support parent changes");
  if (superclass_changed) {
    Decision superclass = CheckSuperclass(live, incoming, hierarchy, abilities);
    if (superclass.error != JVMTI_ERROR_NONE) return superclass;
  }
  Decision interfaces = CheckInterfaces(live, incoming, hierarchy, abilities);
  if (interfaces.error != JVMTI_ERROR_NONE) return interfaces;
  // Match ti_class.cc: nested classes use the InnerClass annotation's flags.
  const uint32_t class_changed =
      (ClassModifiers(live.access) ^
       ClassModifiers(incoming.reflection_access.value_or(incoming.access))) |
      ((live.access ^ incoming.access) & kClassDefinitionFlags);
  const bool kind_changed = class_changed & kClassKinds;
  if (class_changed & ~(kClassAllowed | kClassKinds) ||
      (kind_changed && (!abilities.class_kinds || !owns_member_mapping)))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class kind change requires verified allocation, mapping and invocation guards");
  if (interface_kind_changed && hierarchy.loaded_subclasses)
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION,
                  "class/interface conversion invalidates a loaded subtype's superclass or interface list");
  const bool becomes_final = (incoming.access & acc::kAccFinal) && !(live.access & acc::kAccFinal);
  if (becomes_final && hierarchy.loaded_subclasses)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class made final has loaded subclasses");
  const bool becomes_abstract =
      (incoming.access & acc::kAccAbstract) && !(live.access & acc::kAccAbstract);
  const bool instance_precheck = interface_kind_changed && incoming_interface;
  if ((becomes_abstract || instance_precheck) && !abilities.instance_check)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class modifier or kind change cannot be checked for instances");

  Decision decision;
  decision.requires_no_instances = becomes_abstract || instance_precheck;
  decision.requires_instance_precheck = instance_precheck;
  decision.interface_kind_changed = interface_kind_changed;
  decision.published_verification = (class_changed & ~kAccSuper) == acc::kAccEnum &&
      !superclass_changed && !parents_changed && SameMembers(live.fields, incoming.fields, false) &&
      SameMembers(live.methods, incoming.methods, true);
  decision.superclass_changed = superclass_changed;
  decision.interfaces_removed = interfaces.interfaces_removed;
  // Copies of the removed interfaces' methods vanish from the re-linked classes.
  decision.requires_member_mapping = kind_changed || superclass_changed || interfaces.interfaces_removed;
  if (incoming_interface && parents_changed) {
    // CheckInterfaces already rejected removed or unresolvable parents.
    Decision parents = CheckInterfaceParents(incoming, hierarchy, abilities.interface_selection, abilities.interface_symbols);
    if (parents.error != JVMTI_ERROR_NONE) return parents;
    decision.requires_member_mapping = true;
  }
  // Only a new Class object carries the new flags or interface table: take
  // ART's structural path.
  if ((class_changed & ~kAccSuper) || superclass_changed || live.interfaces != incoming.interfaces)
    decision.methods_structural = true;
  std::map<std::tuple<std::string, std::string, bool>, uint32_t> new_fields;
  for (const auto& f : incoming.fields)
    new_fields[{f.name, f.descriptor, f.IsStatic()}] = FieldModifiers(f.access);
  for (const auto& old : live.fields) {
    auto it = new_fields.find({old.name, old.descriptor, old.IsStatic()});
    if (it == new_fields.end()) {
      if (!(owns_member_mapping && retires_fields) || (interface_target && !interface_members))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                      "field removed or its type/storage changed: " + old.name + ":" + old.descriptor);
      decision.fields_structural = true;
      decision.retired_fields.push_back(old);
      decision.requires_member_mapping = true;
      continue;
    }
    const uint32_t changed = old.access ^ it->second;
    const uint32_t allowed = interface_target ? kAccSynthetic : kFieldAllowed;
    if ((changed & ~allowed) || (changed && interface_target && !interface_members))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "unsupported field modifier change: " + old.name);
    if (changed) {
      decision.fields_structural = true;
      decision.requires_member_mapping |= interface_target;
    }
    new_fields.erase(it);
  }
  if (!new_fields.empty()) {
    // An interface is initialized before its redefinition, so an added static
    // starts from its default value; ART never runs an added initializer.
    if (interface_target && !interface_members)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "interface redefinition does not support added fields");
    if (interface_target) decision.requires_member_mapping = true;
    decision.fields_structural = true;
  }

  std::map<MemberKey, uint32_t> new_methods;
  for (const auto& m : incoming.methods) new_methods[{m.name, m.descriptor}] = MethodModifiers(m.access);
  for (const auto& old : live.methods) {
    auto it = new_methods.find({old.name, old.descriptor});
    if (it == new_methods.end()) {
      if (!owns_member_mapping)
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
                      "unsupported method retirement: " + Describe(old));
      // A retired default/abstract declaration also retires the copies its
      // re-linked implementations no longer carry; their slots go with it.
      if (interface_target && !interface_members)
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
                      "unsupported interface method retirement: " + Describe(old));
      decision.methods_structural = true;
      decision.retired_methods.push_back(old);
      decision.requires_member_mapping = true;
      continue;
    }
    const uint32_t now = it->second;
    const uint32_t changed = old.access ^ now;
    if (BreaksVerifiedCallers(old.name, old.access, now)) {
      if (!owns_member_mapping || !abilities.kind_changes || (interface_target && !interface_members))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                      "verified callers may still invoke the old kind of " + Describe(old));
      // The old kind retires; the declaration of the new kind is a new member,
      // which an interface target checks like any other addition.
      decision.kind_changes.push_back({{old.name, old.descriptor}, old.access});
      decision.retired_methods.push_back(old);
      decision.methods_structural = true;
      decision.requires_member_mapping = true;
      if (!interface_target) new_methods.erase(it);
      continue;
    }
    if (interface_target && incoming_interface && changed) {
      Decision kind = CheckInterfaceKindChange({old.name, old.descriptor}, old.access, now, hierarchy,
                                               interface_members, abilities.interface_selection);
      if (kind.error != JVMTI_ERROR_NONE) return kind;
      decision.requires_member_mapping = true;
    }
    // Abstract <-> concrete and Java <-> native change the method's kind; the
    // structural path creates the new kind. Native -> Java needs the engine's
    // commit, which keeps ART from copying the old JNI entry into a Java method.
    uint32_t unsupported = changed & ~(kMethodAllowed | acc::kAccAbstract);
    if ((now & acc::kAccNative) || owns_member_mapping) unsupported &= ~acc::kAccNative;
    if (unsupported)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "unsupported method modifier change: " + Describe(old));
    const auto& overrides = (now & (acc::kAccPublic | acc::kAccProtected))
        ? hierarchy.visible : hierarchy.same_runtime_package;
    if ((now & acc::kAccFinal) && !(old.access & acc::kAccFinal) && Virtual(now) &&
        !Initializer(old.name) &&
        overrides.count({old.name, old.descriptor}))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "method made final is overridden by a loaded subclass: " + Describe(old));
    if ((changed & acc::kAccNative) && !(now & acc::kAccNative))
      decision.requires_member_mapping = true;
    if (changed) decision.methods_structural = true;
    new_methods.erase(it);
  }
  for (const auto& [key, now] : new_methods) {
    const auto before = retired.find(key);
    if (before == retired.end() || !BreaksVerifiedCallers(key.first, before->second, now)) continue;
    if (!owns_member_mapping || !abilities.kind_changes || (interface_target && !interface_members))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "verified callers may still invoke the retired kind of " + key.first + key.second);
    decision.kind_changes.push_back({key, before->second});
    decision.requires_member_mapping = true;
  }
  if (incoming_interface) {
    for (const auto& [key, now] : new_methods) {
      Decision added = CheckInterfaceAddition(key, now, hierarchy, interface_members, abilities.interface_selection, abilities.interface_symbols);
      if (added.error != JVMTI_ERROR_NONE) return added;
      decision.requires_member_mapping = true;
    }
  }
  if (!new_methods.empty()) decision.methods_structural = true;
  // With the engine's mapping, a vanished copy maps to its canonical method.
  Decision copied = CheckCopiedMembers(live, incoming, hierarchy);
  if (copied.error != JVMTI_ERROR_NONE) {
    if (!owns_member_mapping) return copied;
    decision.requires_member_mapping = true;
  }
  return decision;
}
}  // namespace artdcevm::structural_policy

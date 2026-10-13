#include "platform/common/structural_policy.h"
#include <map>
#include <set>
#include <tuple>

namespace artdcevm::structural_policy {
namespace {
using evolution::ClassSchema;
using evolution::Member;
namespace acc = evolution;

constexpr uint32_t kJavaFlags = 0xffff;
constexpr uint32_t kAccessLevel = acc::kAccPublic | acc::kAccPrivate | acc::kAccProtected;
constexpr uint32_t kClassDefinitionFlags = kJavaFlags & ~(kAccessLevel | acc::kAccStatic);
constexpr uint32_t kAccSuper = 0x0020;
constexpr uint32_t kAccSynthetic = 0x1000;
// Class modifiers a structural redefinition may change: the new Class takes
// its flags from the new ClassDef (and reflection from its InnerClass entry).
// Interface, annotation and enum kinds stay fixed.
constexpr uint32_t kClassAllowed = kAccessLevel | acc::kAccStatic | acc::kAccFinal | kAccSuper |
    acc::kAccAbstract | kAccSynthetic;
constexpr uint32_t kFieldTransient = 0x0080;
constexpr uint32_t kFieldAllowed = kAccessLevel | acc::kAccFinal | acc::kAccVolatile | kFieldTransient;
constexpr uint32_t kMethodAllowed = kAccessLevel | acc::kAccFinal | acc::kAccSynchronized;

Decision Reject(jvmtiError error, std::string reason) {
  Decision decision;
  decision.error = error;
  decision.reason = std::move(reason);
  return decision;
}

std::string Describe(const Member& m) { return m.name + m.descriptor; }

bool Initializer(const std::string& name) { return name == "<init>" || name == "<clinit>"; }

// Static and private methods never occupy a vtable or iftable slot.
bool Virtual(uint32_t modifiers) { return !(modifiers & (acc::kAccStatic | acc::kAccPrivate)); }

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
// Removing an interface narrows the type of live instances, and copied
// default/miranda methods may disappear from the re-linked classes.
Decision CheckInterfaces(const ClassSchema& live, const ClassSchema& incoming,
                         const LoadedHierarchy& hierarchy, const Abilities& abilities) {
  const std::set<std::string> before(live.interfaces.begin(), live.interfaces.end());
  const std::set<std::string> after(incoming.interfaces.begin(), incoming.interfaces.end());
  if (after.size() != incoming.interfaces.size())
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "duplicate interface");
  for (const auto& old : live.interfaces) {
    if (after.count(old)) continue;
    if (!abilities.member_mapping)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                    "interface removal requires the engine's member mapping: " + old);
    if (hierarchy.narrowing_checked != incoming.interfaces)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                    "interface removal was not checked against live references: " + old);
  }
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
  return {};
}
}  // namespace

uint32_t ClassModifiers(uint32_t dex_access) { return dex_access & kJavaFlags; }
uint32_t FieldModifiers(uint32_t dex_access) { return dex_access & kJavaFlags; }
uint32_t MethodModifiers(uint32_t dex_access) {
  uint32_t modifiers = dex_access;
  if (modifiers & acc::kAccAbstract) modifiers &= ~acc::kAccNative;
  modifiers &= ~acc::kAccSynchronized;
  if (modifiers & acc::kAccDeclaredSynchronized) modifiers |= acc::kAccSynchronized;
  return modifiers & kJavaFlags;
}

Decision Evaluate(const ClassSchema& live, const ClassSchema& incoming,
                  const LoadedHierarchy& hierarchy, const Abilities& abilities) {
  const bool owns_member_mapping = abilities.member_mapping;
  const bool retires_fields = abilities.member_mapping && abilities.field_retirement;
  if (live.superclass != incoming.superclass)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "superclass changed");
  Decision interfaces = CheckInterfaces(live, incoming, hierarchy, abilities);
  if (interfaces.error != JVMTI_ERROR_NONE) return interfaces;
  // Match ti_class.cc: nested classes use the InnerClass annotation's flags.
  const uint32_t class_changed =
      (ClassModifiers(live.access) ^
       ClassModifiers(incoming.reflection_access.value_or(incoming.access))) |
      ((live.access ^ incoming.access) & kClassDefinitionFlags);
  if (class_changed & ~kClassAllowed)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class kind changed (interface, annotation or enum)");
  const bool becomes_final = (incoming.access & acc::kAccFinal) && !(live.access & acc::kAccFinal);
  if (becomes_final && hierarchy.loaded_subclasses)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class made final has loaded subclasses");
  const bool becomes_abstract =
      (incoming.access & acc::kAccAbstract) && !(live.access & acc::kAccAbstract);
  if (becomes_abstract && !abilities.instance_check)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class made abstract cannot be checked for instances");

  Decision decision;
  decision.requires_no_instances = becomes_abstract;
  // Only a new Class object carries the new flags or interface table: take
  // ART's structural path.
  if ((class_changed & ~kAccSuper) || live.interfaces != incoming.interfaces)
    decision.methods_structural = true;
  std::map<std::tuple<std::string, std::string, bool>, uint32_t> new_fields;
  for (const auto& f : incoming.fields)
    new_fields[{f.name, f.descriptor, f.IsStatic()}] = FieldModifiers(f.access);
  for (const auto& old : live.fields) {
    auto it = new_fields.find({old.name, old.descriptor, old.IsStatic()});
    if (it == new_fields.end()) {
      if (!(owns_member_mapping && retires_fields))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                      "field removed or its type/storage changed: " + old.name + ":" + old.descriptor);
      decision.fields_structural = true;
      decision.retired_fields.push_back(old);
      continue;
    }
    const uint32_t changed = old.access ^ it->second;
    if (changed & ~kFieldAllowed)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "unsupported field modifier change: " + old.name);
    if (changed) decision.fields_structural = true;
    new_fields.erase(it);
  }
  if (!new_fields.empty()) decision.fields_structural = true;

  std::map<MemberKey, uint32_t> new_methods;
  for (const auto& m : incoming.methods) new_methods[{m.name, m.descriptor}] = MethodModifiers(m.access);
  for (const auto& old : live.methods) {
    auto it = new_methods.find({old.name, old.descriptor});
    if (it == new_methods.end()) {
      if (!owns_member_mapping || (live.access & acc::kAccInterface) ||
          (old.access & (acc::kAccNative | acc::kAccAbstract)) || Initializer(old.name))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
                      "unsupported method retirement: " + Describe(old));
      decision.methods_structural = true;
      decision.retired_methods.push_back(old);
      continue;
    }
    const uint32_t now = it->second;
    const uint32_t changed = old.access ^ now;
    uint32_t unsupported = changed & ~kMethodAllowed;
    if (unsupported == acc::kAccNative && (now & acc::kAccNative)) unsupported = 0;  // Java -> native
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
    if (changed) decision.methods_structural = true;
    new_methods.erase(it);
  }
  if (!new_methods.empty()) decision.methods_structural = true;
  // With the engine's mapping, a vanished copy maps to its canonical method.
  if (!owns_member_mapping) {
    Decision copied = CheckCopiedMembers(live, incoming, hierarchy);
    if (copied.error != JVMTI_ERROR_NONE) return copied;
  }
  return decision;
}
}  // namespace artdcevm::structural_policy

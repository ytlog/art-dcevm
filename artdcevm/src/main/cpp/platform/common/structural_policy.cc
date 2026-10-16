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

// Verified code resolves a method by name and signature without checking that
// it still has the invoke kind it was verified against, then calls it as that
// kind: a static method with a receiver argument or an instance method
// without one, or a private method through the vtable index it lacks. A
// method identity therefore keeps being static or not, and once virtual stays
// virtual. A private method may become virtual: only the class's own code
// invokes it directly, which stays valid.
// Constructors and initializers are always invoked directly.
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
// Removing an interface would narrow the type of live instances.
Decision CheckInterfaces(const ClassSchema& live, const ClassSchema& incoming,
                         const LoadedHierarchy& hierarchy) {
  const std::set<std::string> before(live.interfaces.begin(), live.interfaces.end());
  const std::set<std::string> after(incoming.interfaces.begin(), incoming.interfaces.end());
  if (after.size() != incoming.interfaces.size())
    return Reject(JVMTI_ERROR_FAILS_VERIFICATION, "duplicate interface");
  for (const auto& old : live.interfaces)
    if (!after.count(old))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                    "interface removed: " + old);
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
  if (superclass.related_in_batch)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "superclass change shares the batch with a related class");
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
// Only new code invokes an added static or private interface method. An added
// default fills a new iftable slot that ART links into every re-linked
// subtype. Abstract additions require checking the actual linked dispatch
// of every concrete implementation. `modifiers` is the JVMTI view.
Decision CheckInterfaceAddition(const MemberKey& key, uint32_t modifiers,
                                const LoadedHierarchy& hierarchy, bool supported, bool abstract_dispatch) {
  const std::string method = key.first + key.second;
  if (!supported)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "interface redefinition does not support added methods: " + method);
  if (Initializer(key.first))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "an initialized interface never runs an added initializer: " + method);
  if (modifiers & acc::kAccNative)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "interface redefinition does not support added native methods: " + method);
  if (!Virtual(modifiers)) {
    // Verified invoke-interface callers would resolve the direct method and
    // dispatch it as the virtual method it is not.
    if (hierarchy.inherited_interface.count(key))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                    "added static or private method hides an inherited interface method: " + method);
    return {};
  }
  if (!(modifiers & acc::kAccPublic))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "added interface instance method is neither public nor private: " + method);
  if (modifiers & acc::kAccAbstract) {
    if (!abstract_dispatch)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                    "added abstract interface method requires a verified IfTable accessor: " + method);
    Decision pending;
    pending.interface_obligations.push_back(key);
    return pending;
  }
  const auto shadow = hierarchy.subtype_shadows.find(key);
  if (shadow != hierarchy.subtype_shadows.end())
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                  "added default method may change calls resolved by " + shadow->second + ": " + method);
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
                  const LoadedHierarchy& hierarchy, const Abilities& abilities,
                  const RetiredMethods& retired) {
  const bool interface_target = live.access & acc::kAccInterface;
  const bool owns_member_mapping = abilities.member_mapping;
  const bool retires_fields = abilities.member_mapping && abilities.field_retirement;
  const bool interface_members = abilities.member_mapping && abilities.interface_members;
  const bool superclass_changed = live.superclass != incoming.superclass;
  if (interface_target && (superclass_changed || live.interfaces != incoming.interfaces))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "interface redefinition does not support parent changes");
  if (superclass_changed) {
    Decision superclass = CheckSuperclass(live, incoming, hierarchy, abilities);
    if (superclass.error != JVMTI_ERROR_NONE) return superclass;
  }
  Decision interfaces = CheckInterfaces(live, incoming, hierarchy);
  if (interfaces.error != JVMTI_ERROR_NONE) return interfaces;
  // Match ti_class.cc: nested classes use the InnerClass annotation's flags.
  const uint32_t class_changed =
      (ClassModifiers(live.access) ^
       ClassModifiers(incoming.reflection_access.value_or(incoming.access))) |
      ((live.access ^ incoming.access) & kClassDefinitionFlags);
  if (interface_target && (class_changed & ~kAccSuper))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "interface redefinition does not support class modifier changes");
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
  decision.superclass_changed = superclass_changed;
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
      if (interface_target || !(owns_member_mapping && retires_fields))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                      "field removed or its type/storage changed: " + old.name + ":" + old.descriptor);
      decision.fields_structural = true;
      decision.retired_fields.push_back(old);
      continue;
    }
    const uint32_t changed = old.access ^ it->second;
    if (changed && (interface_target || (changed & ~kFieldAllowed)))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "unsupported field modifier change: " + old.name);
    if (changed) decision.fields_structural = true;
    new_fields.erase(it);
  }
  if (!new_fields.empty()) {
    if (interface_target)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "interface redefinition does not support added fields");
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
      // An interface's virtual methods own iftable slots in every implementation.
      if (interface_target && (!interface_members || Virtual(old.access)))
        return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
                      "unsupported interface method retirement: " + Describe(old));
      decision.methods_structural = true;
      decision.retired_methods.push_back(old);
      continue;
    }
    const uint32_t now = it->second;
    const uint32_t changed = old.access ^ now;
    if (interface_target && changed)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "interface redefinition does not support method modifier changes: " + Describe(old));
    if (BreaksVerifiedCallers(old.name, old.access, now))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "verified callers may still invoke the old kind of " + Describe(old));
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
    if (changed) decision.methods_structural = true;
    new_methods.erase(it);
  }
  for (const auto& [key, now] : new_methods) {
    const auto before = retired.find(key);
    if (before != retired.end() && BreaksVerifiedCallers(key.first, before->second, now))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "verified callers may still invoke the retired kind of " + key.first + key.second);
  }
  if (interface_target) {
    for (const auto& [key, now] : new_methods) {
      Decision added = CheckInterfaceAddition(key, now, hierarchy, interface_members,
                                              abilities.interface_abstract_methods);
      if (added.error != JVMTI_ERROR_NONE) return added;
      decision.interface_obligations.insert(decision.interface_obligations.end(),
          added.interface_obligations.begin(), added.interface_obligations.end());
    }
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

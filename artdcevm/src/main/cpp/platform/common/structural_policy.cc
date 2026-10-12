#include "platform/common/structural_policy.h"
#include <map>
#include <tuple>

namespace artdcevm::structural_policy {
namespace {
using evolution::ClassSchema;
using evolution::Member;
namespace acc = evolution;

constexpr uint32_t kJavaFlags = 0xffff;
constexpr uint32_t kAccessLevel = acc::kAccPublic | acc::kAccPrivate | acc::kAccProtected;
constexpr uint32_t kClassDefinitionFlags = kJavaFlags & ~(kAccessLevel | acc::kAccStatic);
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
                  const SubclassMethods& subclass_methods) {
  if (live.superclass != incoming.superclass || live.interfaces != incoming.interfaces)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "superclass or interfaces changed");
  // Match ti_class.cc: nested classes use the InnerClass annotation's flags.
  if (ClassModifiers(live.access) !=
          ClassModifiers(incoming.reflection_access.value_or(incoming.access)) ||
      (live.access & kClassDefinitionFlags) != (incoming.access & kClassDefinitionFlags))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class modifiers changed");

  Decision decision;
  std::map<std::tuple<std::string, std::string, bool>, uint32_t> new_fields;
  for (const auto& f : incoming.fields)
    new_fields[{f.name, f.descriptor, f.IsStatic()}] = FieldModifiers(f.access);
  for (const auto& old : live.fields) {
    auto it = new_fields.find({old.name, old.descriptor, old.IsStatic()});
    if (it == new_fields.end())
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                    "field removed or its type/storage changed: " + old.name + ":" + old.descriptor);
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
    if (it == new_methods.end())
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
                    "method removed or its signature changed: " + Describe(old));
    const uint32_t now = it->second;
    const uint32_t changed = old.access ^ now;
    uint32_t unsupported = changed & ~kMethodAllowed;
    if (unsupported == acc::kAccNative && (now & acc::kAccNative)) unsupported = 0;  // Java -> native
    if (unsupported)
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "unsupported method modifier change: " + Describe(old));
    const auto& overrides = (now & (acc::kAccPublic | acc::kAccProtected))
        ? subclass_methods.visible : subclass_methods.same_runtime_package;
    if ((now & acc::kAccFinal) && !(old.access & acc::kAccFinal) &&
        !(now & (acc::kAccPrivate | acc::kAccStatic)) &&
        old.name != "<init>" && old.name != "<clinit>" &&
        overrides.count({old.name, old.descriptor}))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "method made final is overridden by a loaded subclass: " + Describe(old));
    if (changed) decision.methods_structural = true;
    new_methods.erase(it);
  }
  if (!new_methods.empty()) decision.methods_structural = true;
  return decision;
}
}  // namespace artdcevm::structural_policy

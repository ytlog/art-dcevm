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
// Class bits whose change alters instantiation, dispatch or the class kind.
constexpr uint32_t kClassKind = acc::kAccFinal | acc::kAccInterface | acc::kAccAbstract | 0x2000 | 0x4000;
constexpr uint32_t kFieldTransient = 0x0080;
constexpr uint32_t kFieldAllowed = kAccessLevel | acc::kAccFinal | acc::kAccVolatile | kFieldTransient;
constexpr uint32_t kMethodVarargs = 0x0080, kMethodStrict = 0x0800;
constexpr uint32_t kMethodAllowed = acc::kAccPublic | acc::kAccProtected | acc::kAccFinal |
    acc::kAccSynchronized | kMethodVarargs | kMethodStrict;

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
                  const std::set<MemberKey>& subclass_methods) {
  if (live.superclass != incoming.superclass || live.interfaces != incoming.interfaces)
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                  "superclass or interfaces changed");
  // JVMTI reports InnerClass flags for nested classes, so only the class kind
  // bits are comparable; a new access level is taken from the new ClassDef.
  if ((live.access & kClassKind) != (ClassModifiers(incoming.access) & kClassKind))
    return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
                  "class final/abstract/interface/annotation/enum modifiers changed");

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
    if ((now & acc::kAccFinal) && !(old.access & acc::kAccFinal) &&
        subclass_methods.count({old.name, old.descriptor}))
      return Reject(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                    "method made final is overridden by a loaded subclass: " + Describe(old));
    if (changed) decision.methods_structural = true;
    new_methods.erase(it);
  }
  if (!new_methods.empty()) decision.methods_structural = true;
  return decision;
}
}  // namespace artdcevm::structural_policy

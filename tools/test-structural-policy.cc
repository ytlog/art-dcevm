#include "platform/common/structural_policy.h"
#include <cassert>
#include <iostream>

namespace e = artdcevm::evolution;
namespace p = artdcevm::structural_policy;

int main() {
  e::ClassSchema live;
  live.descriptor = "Lfixture/Subject;";
  live.superclass = "Ljava/lang/Object;";
  live.access = e::kAccPublic;
  live.fields = {{"value", "I", e::kAccProtected}};
  live.methods = {{"compute", "()I", e::kAccProtected}};
  auto incoming = live;
  auto evaluate = [&] { return p::Evaluate(live, incoming, {}); };
  assert(evaluate().error == JVMTI_ERROR_NONE);
  assert(!evaluate().fields_structural && !evaluate().methods_structural);
  incoming.fields.push_back({"added", "J", e::kAccPublic});
  assert(evaluate().fields_structural);
  incoming = live;
  incoming.methods.push_back({"addedMethod", "()I", e::kAccPublic});
  assert(evaluate().methods_structural);
  incoming = live;
  incoming.fields.clear();
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
  incoming = live;
  incoming.fields[0].descriptor = "J";
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
  incoming = live;
  incoming.fields[0].access |= e::kAccStatic;
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
  incoming = live;
  incoming.methods.clear();
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
  incoming = live;
  incoming.methods[0].descriptor = "(I)I";
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
  incoming = live;
  incoming.superclass = "Lfixture/Parent;";
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
  incoming = live;
  incoming.access |= e::kAccFinal;
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
  incoming = live;
  incoming.methods[0].access = e::kAccPrivate;
  assert(evaluate().error == JVMTI_ERROR_NONE && evaluate().methods_structural);
  incoming.methods[0].access = e::kAccPublic | e::kAccDeclaredSynchronized;
  assert(evaluate().error == JVMTI_ERROR_NONE && evaluate().methods_structural);
  incoming.methods[0].access = e::kAccPublic | e::kAccNative;
  assert(evaluate().error == JVMTI_ERROR_NONE && evaluate().methods_structural);
  live.methods[0].access |= e::kAccNative;
  incoming = live;
  incoming.methods[0].access &= ~e::kAccNative;
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
  live.methods[0].access = e::kAccPublic;
  incoming = live;
  incoming.methods[0].access |= e::kAccAbstract;
  assert(evaluate().error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
  incoming = live;
  incoming.methods[0].access |= e::kAccFinal;
  assert(p::Evaluate(live, incoming, {{"compute", "()I"}}).error ==
         JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
  live.methods[0].access = e::kAccPrivate;
  incoming = live;
  incoming.methods[0].access |= e::kAccFinal;
  assert(p::Evaluate(live, incoming, {{"compute", "()I"}}).error == JVMTI_ERROR_NONE);
  assert(p::MethodModifiers(e::kAccPublic | e::kAccConstructor) == e::kAccPublic);
  assert(p::MethodModifiers(e::kAccPublic | e::kAccDeclaredSynchronized) ==
         (e::kAccPublic | e::kAccSynchronized));
  assert(p::MethodModifiers(e::kAccPublic | e::kAccAbstract | e::kAccNative) ==
         (e::kAccPublic | e::kAccAbstract));
  std::cout << "Phase 2 policy regression checks passed\n";
}

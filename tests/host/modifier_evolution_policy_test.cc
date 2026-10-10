#include "platform/common/native_schema_policy.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
using artdcevm::native_schema_policy::CheckClassModifierEvolution;
using artdcevm::native_schema_policy::CheckMethodModifierEvolution;
using Action = artdcevm::native_schema_policy::ModifierEvolutionAction;
namespace {
constexpr uint32_t kPublic=1, kPrivate=2, kProtected=4, kStatic=8, kFinal=16;
constexpr uint32_t kSync=32, kNative=256, kAbstract=1024, kDeclaredSync=0x20000;
struct Case { uint32_t before, after; bool direct; Action expected; };
}
int main() {
  const Case methods[] = {
    {kPublic, kPublic, false, Action::kUnchanged},
    {kPublic, kPublic|kStatic, true, Action::kRecreate},
    {kPrivate, kPublic, false, Action::kRecreate},
    {kPublic, kPrivate, true, Action::kRecreate},
    {kPrivate|kStatic, kPublic|kStatic, true, Action::kRecreate},
    {kPublic, kProtected, false, Action::kRecreate},
    {kPublic, 0, false, Action::kRecreate},
    {0, kProtected|kSync|kDeclaredSync, false, Action::kRecreate},
    {kPublic, kPublic|kSync, false, Action::kRecreate},
    {kPublic|kSync|kDeclaredSync, kPublic, false, Action::kRecreate},
    {kPublic|kSync, kPublic|kDeclaredSync, false, Action::kRecreate},
    {kPublic, kPublic|kFinal, false, Action::kCheckFinalOverride},
    {kPublic, kPublic|kFinal|kDeclaredSync, false, Action::kCheckFinalOverride},
    {kPublic|kSync, kPublic|kFinal|kDeclaredSync, false, Action::kCheckFinalOverride},
    {kPrivate, kPrivate|kFinal, true, Action::kRecreate},
    {kPublic|kStatic, kPublic|kStatic|kFinal, true, Action::kRecreate},
    {kPublic|kFinal, kPublic, false, Action::kRecreate},
    {kPublic|kFinal|kDeclaredSync, kPublic, false, Action::kRecreate},
    {kPublic, kPublic|kAbstract, false, Action::kCheckAbstractDependents},
    {kPublic|kAbstract, kPublic, false, Action::kRecreate},
    {kPrivate, kPrivate|kAbstract, true, Action::kReject},
    {kPublic|kStatic, kPublic|kStatic|kAbstract, true, Action::kReject},
    {kPublic, kPublic|kNative, false, Action::kRecreate},
    {kPublic|kNative|kStatic, kPublic|kStatic, true, Action::kRecreate},
    {kPublic, kPublic|0x1000, false, Action::kReject}, // synthetic
    {kPublic, kPublic|0x40, false, Action::kReject}, // bridge
    {kPublic, kPublic|0x80, false, Action::kReject}, // varargs
    {kPublic, kPrivate|kSync, true, Action::kReject},
    {kPublic, kProtected|kFinal, false, Action::kReject},
    {kPublic, kPublic|kFinal|kNative, false, Action::kReject},
    {kPublic, kPublic|kAbstract|kSync, false, Action::kReject},
    {kPublic, kPublic|kStatic|kSync, true, Action::kReject},
  };
  unsigned checked=0;
  for (const auto& c : methods) {
    assert(CheckMethodModifierEvolution(c.before,c.after,true,c.direct)==c.expected);
    assert(CheckMethodModifierEvolution(c.before,c.after,false,c.direct)==
        (c.before==c.after ? Action::kUnchanged : Action::kReject));
    checked+=2;
  }
  const Case classes[] = {
    {kPublic,kPublic,false,Action::kUnchanged},
    {kPublic,kPublic|kFinal,false,Action::kCheckLoadedSubclass},
    {kPublic|kFinal,kPublic,false,Action::kRecreate},
    {kPublic,kPublic|kAbstract,false,Action::kRecreate},
    {kPublic|kAbstract,kPublic,false,Action::kRecreate},
    {kPublic,kPublic|kFinal|kAbstract,false,Action::kReject},
    {kPublic,kProtected,false,Action::kReject},
    {kPublic,kPublic|0x200,false,Action::kReject}, // interface
    {kPublic,kPublic|0x1000,false,Action::kReject},
  };
  for (const auto& c : classes) {
    assert(CheckClassModifierEvolution(c.before,c.after,true)==c.expected);
    assert(CheckClassModifierEvolution(c.before,c.after,false)==
        (c.before==c.after ? Action::kUnchanged : Action::kReject));
    checked+=2;
  }
  // Unknown ART/runtime flag changes must not silently enter a supported Java
  // modifier route; unchanged flags preserve declarations without recreation.
  const uint32_t supported=kPublic|kPrivate|kProtected|kStatic|kFinal|kSync|
      kNative|kAbstract|kDeclaredSync;
  for (unsigned bit=0;bit<32;++bit) {
    const uint32_t flag=uint32_t{1}<<bit;
    assert(CheckMethodModifierEvolution(flag,flag,true,false)==Action::kUnchanged);
    assert(CheckClassModifierEvolution(flag,flag,true)==Action::kUnchanged);
    checked+=2;
    if ((flag&supported)==0) {
      assert(CheckMethodModifierEvolution(kPublic,kPublic|flag,true,false)==Action::kReject);
      assert(CheckMethodModifierEvolution(kPublic|flag,kPublic,true,true)==Action::kReject);
      checked+=2;
    }
  }
  // The common owner contains two deliberately different contracts. A legal
  // combined transition can be planned by the native schema engine while the
  // original structural hook must still refuse it.
  artdcevm::evolution::ClassSchema before;
  before.descriptor="LPolicy;"; before.superclass="Ljava/lang/Object;";
  before.access=kPublic; before.methods={{"run","()I",kPublic}};
  auto after=before; after.methods[0].access=kProtected|kFinal;
  artdcevm::native_schema_policy::Plan plan; std::string error;
  assert(artdcevm::native_schema_policy::Plan::Prepare(before,after,&plan,&error));
  assert(plan.Requires(artdcevm::native_schema_policy::GuardKind::FinalMethodOverrides));
  assert(CheckMethodModifierEvolution(kPublic,kProtected|kFinal,true,false)==Action::kReject);
  before.access=kPublic|kFinal; after=before; after.access=kPublic|kAbstract;
  assert(artdcevm::native_schema_policy::Plan::Prepare(before,after,&plan,&error));
  assert(plan.Requires(artdcevm::native_schema_policy::GuardKind::ExistingInstances));
  assert(CheckClassModifierEvolution(before.access,after.access,true)==Action::kReject);
  checked+=6;
  std::printf("modifier evolution policy: %u cases passed\n",checked);
}

#pragma once
#include <cstdint>
namespace artdcevm::loaded_member_guards {
// Shared policy with Android13's original loaded-class guard. The source
// adapter supplies rooted Classes, actual public flags and virtual declarations;
// this algorithm does not choose an ART layout or obtain any roots.
template<class Class, class Parent>
bool Subclass(Class source,Class target,Parent parent) {
  for(;source!=Class{};source=parent(source))if(source==target)return true;
  return false;
}
template<class Class,class Parent,class SamePackage>
bool Accessible(Class source,Class target,uint32_t flags,Parent parent,SamePackage package,bool source_interface=false) {
  if(source==target||(flags&1u))return true;
  if(flags&2u)return false;
  if((flags&4u)&&!source_interface&&Subclass(source,target,parent))return true;
  return package(source,target);
}
// Preserve the original source13 policy: an abstract declaration does not end
// the ancestor search. The separate real new-vtable/instance proof additionally
// rejects unresolved abstract slots; this policy alone is not an instance proof.
template<class Class,class Parent,class ConcreteDeclaration>
bool ConcreteBelow(Class source,Class target,Parent parent,ConcreteDeclaration concrete) {
  for(;source!=Class{}&&source!=target;source=parent(source))if(concrete(source))return true;
  return false;
}
}

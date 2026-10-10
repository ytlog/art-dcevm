#pragma once
#include <cstddef>
namespace artdcevm::native_hierarchy_typed_storage {
// Pure relation over the caller's independently rooted source facts. This does
// not grant heap coverage, pause, source ownership, or mutation permission.
// Array Classes retain identity; their component relation follows the exact
// old->new Plan even when the array object is absent from old_instances.
template<class Facts>
bool AssignableAfter(const Facts& facts,void* declared,void* actual)noexcept {
 for(size_t depth=0;depth<256;++depth){
  declared=facts.MappedClass(declared);actual=facts.MappedClass(actual);
  if(!declared||!actual)return false;
  if(declared==actual)return true;
  void* target_component=facts.Component(declared),*value_component=facts.Component(actual);
  if(value_component){
   if(facts.ArraySupertype(declared))return true;
   if(!target_component)return false;
   declared=target_component;actual=value_component;continue;
  }
  if(target_component||facts.Primitive(declared)||facts.Primitive(actual))return false;
  return facts.AssignNamed(declared,actual);
 }
 return false;
}
template<class Facts>
bool ArrayRelationChanged(const Facts& facts,void* klass)noexcept {
 for(size_t depth=0;depth<256;++depth){
  void* component=facts.Component(klass);if(!component)return false;
  if(facts.MappedClass(component)!=component)return true;
  klass=component;
 }
 // Cyclic/unsupported metadata cannot establish an unchanged relation.
 return true;
}
}

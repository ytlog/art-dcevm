#include "platform/common/typed_storage_assignability.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
namespace {bool no_new=false;}
void* operator new(size_t n){if(no_new)std::abort();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
using namespace artdcevm::native_hierarchy_typed_storage;
struct Class {Class* parent=nullptr;Class* component=nullptr;bool primitive=false,array_supertype=false;};
struct Facts {
 Class* before;Class* after;
 void* MappedClass(void*p)const noexcept{return p==before?after:p;}
 void* Component(void*p)const noexcept{return p?static_cast<Class*>(p)->component:nullptr;}
 bool ArraySupertype(void*p)const noexcept{return p&&static_cast<Class*>(p)->array_supertype;}
 bool Primitive(void*p)const noexcept{return p&&static_cast<Class*>(p)->primitive;}
 bool AssignNamed(void*d,void*v)const noexcept{for(auto*c=static_cast<Class*>(v);c;c=c->parent)if(c==d)return true;return false;}
};
int main(){
 Class object{nullptr,nullptr,false,true},cloneable{&object,nullptr,false,true},serializable{&object,nullptr,false,true};
 Class ancestor{&object},before{&ancestor},after{&object},unchanged_child{&ancestor};Facts facts{&before,&after};
 Class object_array{&object,&object},ancestor_array{&object,&ancestor},before_array{&object,&before};
 Class object_2d{&object,&object_array},ancestor_2d{&object,&ancestor_array},before_2d{&object,&before_array};
 Class integer{nullptr,nullptr,true},byte{nullptr,nullptr,true},ints{&object,&integer},bytes{&object,&byte};
 Class user_cloneable{&object};
 no_new=true;
 // Ordinary fields: losing Ancestor rejects only that actual typed value.
 assert(!AssignableAfter(facts,&ancestor,&before));assert(AssignableAfter(facts,&object,&before));
 assert(AssignableAfter(facts,&ancestor,&unchanged_child));assert(AssignableAfter(facts,&before,&before));
 // The existing array Class identity remains; its changed component must be
 // checked even though neither the array value nor its Class was replaced.
 assert(ArrayRelationChanged(facts,&before_array)&&ArrayRelationChanged(facts,&before_2d));
 assert(!ArrayRelationChanged(facts,&ancestor_array)&&!ArrayRelationChanged(facts,&object));
 assert(!AssignableAfter(facts,&ancestor_array,&before_array));assert(AssignableAfter(facts,&object_array,&before_array));
 assert(!AssignableAfter(facts,&ancestor_2d,&before_2d));assert(AssignableAfter(facts,&object_2d,&before_2d));
 assert(AssignableAfter(facts,&object,&before_2d)&&AssignableAfter(facts,&cloneable,&before_array)&&AssignableAfter(facts,&serializable,&before_array));
 assert(!AssignableAfter(facts,&user_cloneable,&before_array));
 assert(AssignableAfter(facts,&ints,&ints)&&!AssignableAfter(facts,&ints,&bytes));assert(!AssignableAfter(facts,&object_array,&ints));
 assert(AssignableAfter(facts,&object,&ints));assert(!AssignableAfter(facts,nullptr,&after));
 // Updated owned roots supply the current identities after a moving GC.
 Class moved_before{&ancestor},moved_after{&object},moved_array{&object,&moved_before};Facts moved{&moved_before,&moved_after};
 assert(!AssignableAfter(moved,&ancestor_array,&moved_array));assert(AssignableAfter(moved,&object_array,&moved_array));
 // A cycle cannot be treated as proof of a complete unchanged/assignable type.
 Class cycle;cycle.component=&cycle;assert(ArrayRelationChanged(facts,&cycle));assert(!AssignableAfter(facts,&ancestor_array,&cycle));
 no_new=false;
 std::cout<<"Typed storage relation: narrowed fields, retained array identity, multidimensional arrays, bootstrap supertypes, primitive arrays, moving identities and malformed cycles PASS\n";
}

#include "platform/common/native_class_assignability.h"
#include <jni.h>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
// Explicit unit original Class leaf model of the pinned Class::IsAssignableFrom
// declaration. The public JNI outcomes below are independent literal witnesses,
// not derived by calling this model. No real Android calibration is claimed.
struct Type {bool primitive=false,interface=false;Type* parent=nullptr;Type* component=nullptr;std::array<Type*,3> implemented{};};
Type object,base,child,other,iface,super_iface,foreign_loader_twin,obsolete_twin;
Type integer,longer,int_array,long_array,object_array,base_array,child_array,child_2d,object_2d,cloneable,serializable;
bool Original(Type*target,Type*source){
 if(target==source)return true;
 if(target==&object)return !source->primitive;
 if(target->interface){for(auto*i:source->implemented)if(i==target)return true;return false;}
 if(source->component){if(!target->component)return target==source->parent;return Original(target->component,source->component);}
 if(source->interface)return false;
 for(auto*p=source;p;p=p->parent)if(p==target)return true;return false;
}
size_t code_calls=0;int malformed=0;bool reverse=false;
size_t Code(void*t,void*s){++code_calls;if(malformed)return malformed==1?2:256;return (reverse?Original(static_cast<Type*>(s),static_cast<Type*>(t)):Original(static_cast<Type*>(t),static_cast<Type*>(s)))?1:0;}
bool ObjectLeaf(void*t,void*s){return Original(static_cast<Type*>(t),static_cast<Type*>(s));}
struct Case {Type*target;Type*source;bool expected;};
std::array<Case,23> cases{{
 {&object,&object,true},{&object,&child,true},{&child,&object,false},
 {&base,&child,true},{&child,&base,false},{&other,&child,false},
 {&iface,&child,true},{&super_iface,&child,true},{&iface,&other,false},
 {&object,&iface,true},{&base,&iface,false},
 {&object_array,&child_array,true},{&base_array,&child_array,true},{&child_array,&base_array,false},
 {&object_2d,&child_2d,true},{&object_array,&child_2d,true},
 {&object_array,&int_array,false},{&int_array,&long_array,false},{&int_array,&int_array,true},
 {&cloneable,&int_array,true},{&serializable,&child_2d,true},
 {&foreign_loader_twin,&child,false},{&obsolete_twin,&child,false}
}};
jboolean Public(JNIEnv*,jclass source,jclass target){for(const auto&c:cases)if(c.source==reinterpret_cast<Type*>(source)&&c.target==reinterpret_cast<Type*>(target))return c.expected;assert(false);return false;}
class Hooks:public HookEngine {public:bool object_leaf=false,missing=false;
 bool Initialize(std::string*)override{return true;}
 void*FindSymbol(const char*lib,const char*n)override{assert(!std::strcmp(lib,"libart.so"));if(missing)return nullptr;
 if(!std::strcmp(n,abi::arm64::android_common::kClassAssignableObjPtr))return object_leaf?reinterpret_cast<void*>(ObjectLeaf):nullptr;
 if(!std::strcmp(n,abi::arm64::android_common::kClassAssignableCode))return reinterpret_cast<void*>(Code);return nullptr;}
 void*Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
}
int main(){
 base.parent=&object;child.parent=&base;other.parent=&object;foreign_loader_twin.parent=&object;obsolete_twin.parent=&object;
 iface.interface=super_iface.interface=cloneable.interface=serializable.interface=true;child.implemented={&iface,&super_iface,nullptr};
 integer.primitive=longer.primitive=true;
 int_array.component=&integer;long_array.component=&longer;object_array.component=&object;base_array.component=&base;child_array.component=&child;
 child_2d.component=&child_array;object_2d.component=&object_array;
 for(auto*t:{&int_array,&long_array,&object_array,&base_array,&child_array,&child_2d,&object_2d}){t->parent=&object;t->implemented={&cloneable,&serializable,nullptr};}
 JNINativeInterface_ table{};table.IsAssignableFrom=Public;JNIEnv env{&table};
 std::array<native_class_assignability::Service::Oracle,cases.size()> oracles{};
 for(size_t i=0;i<cases.size();++i){const auto&c=cases[i];oracles[i]={c.target,c.source,bool(env.IsAssignableFrom(reinterpret_cast<jclass>(c.source),reinterpret_cast<jclass>(c.target)))};}
 Hooks hooks;native_class_assignability::Service service;bool value=false;
 assert(service.Bind(hooks)&&!service.Calibrated()&&!service.Assignable(&object,&child,&value));
 for(int mode=1;mode<=2;++mode){malformed=mode;assert(!service.Calibrate(oracles.data(),oracles.size())&&!service.Calibrated());}
 malformed=0;reverse=true;assert(!service.Calibrate(oracles.data(),oracles.size()));reverse=false;
 assert(service.Calibrate(oracles.data(),oracles.size()));
 for(const auto&o:oracles){assert(service.Assignable(o.target,o.source,&value)&&value==o.expected);}
 assert(!service.Assignable(nullptr,&child,&value)&&!service.Assignable(&object,&child,nullptr));
 malformed=2;assert(!service.Assignable(&object,&child,&value));malformed=0;
 const auto calls=code_calls;hooks.object_leaf=true;assert(service.Bind(hooks)&&!service.Calibrated());assert(service.Calibrate(oracles.data(),oracles.size()));assert(code_calls==calls);
 hooks.missing=true;assert(!service.Bind(hooks)&&!service.Calibrated()&&!service.Assignable(&object,&child,&value));
 std::cout<<"Named bool/size_t source adapters with independent JNI unit-leaf public matrix: hierarchy/interfaces/primitive/reference+nested arrays/foreign-loader+obsolete identities PASS; canonical encoding/orientation/unbound refusal PASS; no Android or no-escape permission claimed\n";
}

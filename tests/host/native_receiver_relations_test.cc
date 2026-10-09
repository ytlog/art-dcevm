#include "platform/common/native_receiver_relations.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace artdcevm::receiver_guard;
struct Class {Class* parent=nullptr;Class* obsolete=nullptr;void* loader=nullptr;const char* descriptor="Lpkg/Object;";uint32_t flags=0;std::array<Class*,4> interfaces{};std::array<Class*,4> direct{};bool readable=true;};
static bool Read(void*,void*k,ClassView*v)noexcept{auto&c=*static_cast<Class*>(k);if(!c.readable)return false;*v={c.parent,c.loader,c.descriptor,c.flags,0,0};for(auto*i:c.interfaces)if(i)++v->interface_count;for(auto*i:c.direct)if(i)++v->direct_count;return true;}
static bool Iface(void*,void*k,size_t i,void**v)noexcept{*v=static_cast<Class*>(k)->interfaces.at(i);return *v;}
static bool Direct(void*,void*k,size_t i,void**v)noexcept{*v=static_cast<Class*>(k)->direct.at(i);return *v;}
static bool Old(void*,void*k,void**v)noexcept{*v=static_cast<Class*>(k)->obsolete;return true;}
int main(){Relations r({nullptr,Read,Iface,Direct,Old});int loader;Class object,removed,old,current,iface,caller;
 object.descriptor="Ljava/lang/Object;";removed.parent=&object;removed.descriptor="Lpkg/Removed;";old.parent=&removed;old.descriptor="Lpkg/Target;";old.loader=&loader;
 current=old;current.parent=&object;current.obsolete=&old;iface.flags=0x200;iface.descriptor="Lpkg/Iface;";old.interfaces[0]=&iface;
 void*selected=nullptr;bool match=false;
 assert(r.Normalize(&old,&current,&selected)&&selected==&current);assert(r.Assignable(selected,&current,&match)&&match);
 assert(r.Normalize(&removed,&current,&selected)&&selected==&removed);assert(r.Assignable(selected,&current,&match)&&!match);
 assert(r.Assignable(&iface,&old,&match)&&match);assert(r.Assignable(&iface,&current,&match)&&!match);
 // An inherited IfTable edge is insufficient for invokeSpecial.
 caller.parent=&object;caller.interfaces[0]=&iface;assert(r.Special(&iface,&caller,&match)&&!match);caller.direct[0]=&iface;assert(r.Special(&iface,&caller,&match)&&match);
 assert(r.Special(&iface,&iface,&match)&&match);Class subinterface=iface;subinterface.descriptor="Lpkg/Sub;";subinterface.interfaces[0]=&iface;caller.direct[0]=&subinterface;assert(r.Special(&iface,&caller,&match)&&match);
 // Actual retained Class history must be complete, acyclic, same-loader and
 // same-descriptor even when the desired pointer appears before corruption.
 old.obsolete=&current;assert(!r.Normalize(&old,&current,&selected));old.obsolete=nullptr;
 old.readable=false;assert(!r.Normalize(&old,&current,&selected));old.readable=true;
 old.loader=nullptr;assert(!r.Normalize(&old,&current,&selected));old.loader=&loader;
 old.descriptor="Lforeign/Target;";assert(!r.Normalize(&old,&current,&selected));old.descriptor=current.descriptor;
 object.parent=&current;assert(!r.Assignable(&removed,&current,&match));object.parent=nullptr;
 Class malformed=iface;malformed.flags=0;current.interfaces[0]=&malformed;assert(!r.Assignable(&iface,&current,&match));current.interfaces[0]=nullptr;
 // Reread receiver hierarchy after relocation; no process alias table is used.
 Class relocated=current;assert(r.Normalize(&old,&relocated,&selected)&&selected==&relocated);assert(r.Assignable(selected,&relocated,&match)&&match);
 Class array;array.descriptor="[Lpkg/Target;";array.parent=&object;assert(r.Assignable(&object,&array,&match)&&match);
 std::puts("Native receiver relations: retained obsolete chain, removed ancestor/interface, strict direct special interface, cyclic/truncated/foreign metadata, relocation and array Object relation PASS");
}

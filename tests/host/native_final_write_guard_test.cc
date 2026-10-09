#include "platform/common/native_final_write_guard.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::final_write_guard;
struct State {uint32_t flags=0x10;void* field_owner=nullptr;void* method_owner=nullptr;void* obsolete_owner=nullptr;bool available=true;int throws=0;const char* type=nullptr;};
bool Field(void* p,void*,uint32_t* flags,void** owner){auto& s=*static_cast<State*>(p);*flags=s.flags;*owner=s.field_owner;return s.available;}
bool Method(void* p,void*,void** owner){auto& s=*static_cast<State*>(p);*owner=s.method_owner;return s.available;}
bool Equivalent(void* p,void* f,void* m){auto& s=*static_cast<State*>(p);return f==m||(f==s.field_owner&&m==s.obsolete_owner);}
void Raise(void* p,void*,const char* type,const char*){auto& s=*static_cast<State*>(p);++s.throws;s.type=type;}
int main(){
 int thread=0,method=0,field=0,foreign=0,owner=0,obsolete=0,other_thread=0;
 State s;s.field_owner=&owner;s.method_owner=&foreign;
 Guard guard({&s,Field,Method,Equivalent,Raise});const Oracle oracle{&thread,&method,&field};
 assert(!guard.Proven());assert(!guard.EndProof(true,true));
 assert(guard.BeginProof(oracle));assert(!guard.BeginProof(oracle));
 assert(!guard.FieldWritten(&other_thread,&method,&field));assert(!guard.EndProof(true,true));assert(!guard.Proven());
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(!guard.EndProof(false,true));
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(!guard.EndProof(true,false));
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(guard.EndProof(true,true));assert(guard.Proven());
 assert(!std::strcmp(s.type,"Ljava/lang/IllegalAccessError;"));
 // Simulate the actual interpreter contract: pending event exception prevents
 // the write, while mutable/currentowner/pairedobsoleteowner delegates it.
 int value=41;auto write=[&](int next){if(!guard.FieldWritten(&thread,&method,&field))value=next;};
 write(42);assert(value==41);
 s.flags=0;write(42);assert(value==42);assert(!guard.BeginProof(oracle));
 s.flags=0x10;s.method_owner=&owner;write(43);assert(value==43);assert(!guard.BeginProof(oracle));
 s.method_owner=&obsolete;s.obsolete_owner=&obsolete;write(44);assert(value==44);
 s.obsolete_owner=nullptr;write(45);assert(value==44);
 s.available=false;write(46);assert(value==44&&!std::strcmp(s.type,"Ljava/lang/IllegalStateException;"));
 assert(!guard.BeginProof(oracle));assert(!guard.FieldWritten(nullptr,&method,&field));
 std::cout<<"Final bytecode write guard: exact callback proof, exception/store outcome, current flags and obsolete owner semantics passed\n";
}

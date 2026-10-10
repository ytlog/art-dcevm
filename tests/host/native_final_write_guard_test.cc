#include "platform/common/native_final_write_guard.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::final_write_guard;
struct State {uint32_t flags=0x10;void* field_owner=nullptr;void* method_owner=nullptr;void* obsolete_owner=nullptr;bool available=true;int action=0;int throws=0;const char* type=nullptr;};
bool Field(void* p,void*,uint32_t* flags,void** owner){auto& s=*static_cast<State*>(p);*flags=s.flags;*owner=s.field_owner;return s.available;}
bool Method(void* p,void*,void** owner){auto& s=*static_cast<State*>(p);*owner=s.method_owner;return s.available;}
bool Equivalent(void* p,void* f,void* m){auto& s=*static_cast<State*>(p);return f==m||(f==s.field_owner&&m==s.obsolete_owner);}
bool WriterEquivalent(void* p,void*,void*,void*){auto& s=*static_cast<State*>(p);if(s.action==1)s.flags=0;if(s.action==2)s.available=false;return false;}
void Raise(void* p,void*,const char* type,const char*){auto& s=*static_cast<State*>(p);++s.throws;s.type=type;}
int native_method=0;bool flags_available=true;
bool MethodFlags(void*,void* method,uint32_t* flags){*flags=method==&native_method?0x100u:0u;return flags_available;}
int main(){
 int thread=0,method=0,field=0,foreign=0,owner=0,obsolete=0,other_thread=0;
 State s;s.field_owner=&owner;s.method_owner=&foreign;
 Guard guard({&s,Field,Method,Equivalent,Raise,nullptr,MethodFlags});const Oracle oracle{&thread,&method,&field};
 Guard missing({&s,Field,Method,Equivalent,Raise});assert(!missing.BeginProof(oracle));
 assert(!guard.Proven());assert(!guard.EndProof(true,true));
 assert(!guard.BeginProof({&thread,&native_method,&field}));
 assert(guard.BeginProof(oracle));
 assert(!guard.FieldWritten(&thread,&native_method,&field));
 assert(!guard.EndProof(true,true)&&!guard.Proven()); // native event cannot prove bytecode
 assert(guard.BeginProof(oracle));assert(!guard.BeginProof(oracle));
 assert(!guard.FieldWritten(&other_thread,&method,&field));assert(!guard.EndProof(true,true));assert(!guard.Proven());
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(!guard.EndProof(false,true));
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(!guard.EndProof(true,false));
 assert(guard.BeginProof(oracle));assert(guard.FieldWritten(&thread,&method,&field));assert(guard.EndProof(true,true));assert(guard.Proven());
 assert(!std::strcmp(s.type,"Ljava/lang/IllegalAccessError;"));
 const int prior_throws=s.throws;
 assert(!guard.FieldWritten(&thread,&native_method,&field,false));
 assert(!guard.FieldWritten(&thread,&native_method,&field,true));
 assert(s.throws==prior_throws); // original JNI setter retains permissions after publication
 flags_available=false;
 assert(guard.FieldWritten(&thread,&method,&field)&&!std::strcmp(s.type,"Ljava/lang/IllegalStateException;"));
 assert(!guard.BeginProof(oracle));flags_available=true;
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
 State calibration;calibration.field_owner=&owner;calibration.method_owner=&foreign;
 Guard isolated({&calibration,Field,Method,Equivalent,Raise,nullptr,MethodFlags});int unrelated_field=0;
 assert(isolated.BeginCalibration()&&!isolated.CompleteCalibration());
 assert(isolated.BeginProof(oracle));
 assert(!isolated.FieldWritten(&thread,&method,&unrelated_field));
 assert(isolated.FieldWritten(&thread,&method,&field)&&isolated.EndProof(true,true));
 assert(!isolated.FieldWritten(&thread,&method,&unrelated_field));
 assert(!isolated.ValidateProof(isolated.FreezeProof()));
 assert(isolated.FieldWritten(&other_thread,&method,&field)); // exact field remains enforced
 assert(isolated.CompleteCalibration());
 assert(isolated.ValidateProof(isolated.FreezeProof()));
 auto published=isolated.FreezeProof();isolated.CommitProof(published);
 const int before_native=calibration.throws;
 assert(!isolated.FieldWritten(&thread,&native_method,&field)&&calibration.throws==before_native);
 assert(isolated.FieldWritten(&thread,&method,&unrelated_field)); // production enforcement retained
 State refreshed;refreshed.field_owner=&owner;refreshed.method_owner=&foreign;
 Guard native({&refreshed,Field,Method,Equivalent,Raise,WriterEquivalent,MethodFlags});
 assert(native.BeginProof(oracle)&&native.FieldWritten(&thread,&method,&field)&&native.EndProof(true,true));
 refreshed.action=1;assert(!native.FieldWritten(&thread,&method,&field)&&refreshed.flags==0); // flag reread after suspension
 refreshed.flags=0x10;refreshed.action=2;assert(native.FieldWritten(&thread,&method,&field)&&!std::strcmp(refreshed.type,"Ljava/lang/IllegalStateException;"));
 native.ResetUncommittedProof();assert(!native.Proven()&&!native.FieldWritten(&thread,&method,&field));
 std::cout<<"Final bytecode write guard: exact callback proof, exception/store outcome, current flags and obsolete owner semantics passed\n";
}

#include "platform/common/modern_final_field_guard.h"
#include "platform/abi/arm64/android-common/method_handle.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::final_field_guard;
struct State{uint32_t flags=0;int reads=0,throws=0;const char* type=nullptr;bool readable=true;};
bool Read(void* owner,void* field,uint32_t* flags){auto& s=*static_cast<State*>(owner);assert(field==owner);++s.reads;*flags=s.flags;return s.readable;}
void Throw(void* owner,void* self,const char* type,const char*){auto& s=*static_cast<State*>(owner);assert(self==owner);++s.throws;s.type=type;}
int main(){
 artdcevm::abi::arm64::android_common::MethodHandlePrefix prefix{};
 for(uint32_t kind=8;kind<=11;++kind){prefix.kind=kind;assert(artdcevm::abi::arm64::android_common::MethodHandleKind(&prefix)==kind);}
 State s;const Boundary b{&s,Read,Throw};Access a;uint64_t result=99;
 for(auto path:{Path::Bytecode,Path::MethodHandleSetter,Path::VarHandle,Path::Jni,Path::Reflection}){
  a={};a.path=path;a.var_mode_valid=true;a.var_mode_template=1;s.flags=0;
  assert(!Reject(b,&s,&s,a,&result)&&result==99); // current native flags, not input flags
 }
 s.flags=0x10;a={};a.path=Path::Bytecode;
 assert(Reject(b,&s,&s,a,&result)&&result==0&&!std::strcmp(s.type,"Ljava/lang/IllegalAccessError;"));
 a.same_declaring_owner=true;result=99;assert(!Reject(b,&s,&s,a,&result)&&result==99);
 a.path=Path::Jni;a.same_declaring_owner=false;assert(!Reject(b,&s,&s,a,&result));
 a.path=Path::Reflection;a.reflection_override=false;
 assert(Reject(b,&s,&s,a)&&!std::strcmp(s.type,"Ljava/lang/IllegalAccessException;"));
 a.reflection_override=true;assert(!Reject(b,&s,&s,a));
 s.flags=0x18;assert(!Reject(b,&s,&s,a)); // ART accessible reflection also permits static final
 a.path=Path::MethodHandleSetter;assert(Reject(b,&s,&s,a)&&!std::strcmp(s.type,"Ljava/lang/IllegalAccessError;"));
 a.path=Path::VarHandle;a.var_mode_valid=true;
 for(uint32_t mode=0;mode<=4;++mode){a.var_mode_template=mode;result=99;
  const bool rejected=Reject(b,&s,&s,a,&result);assert(rejected==(mode!=0));
  assert(result==(mode?0u:99u));if(mode)assert(!std::strcmp(s.type,"Ljava/lang/UnsupportedOperationException;"));
 }
 a.var_mode_valid=false;assert(Reject(b,&s,&s,a)&&!std::strcmp(s.type,"Ljava/lang/IllegalStateException;"));
 a.var_mode_valid=true;a.var_mode_template=5;assert(Reject(b,&s,&s,a));
 s.readable=false;a.path=Path::Jni;assert(Reject(b,&s,&s,a)); // lost native metadata never delegates
 s.readable=true;s.flags=0;a.path=Path::MethodHandleSetter;assert(!Reject(b,&s,&s,a));
 assert(s.reads&&s.throws);std::cout<<"Final field boundary: fresh native flags, bytecode owner, JNI/reflective override, MH and all VH templates passed\n";
}

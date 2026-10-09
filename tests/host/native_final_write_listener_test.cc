#include "platform/common/native_final_write_listener.h"
#include "platform/abi/arm64/android-common/instrumentation.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;using namespace final_write_guard;
int adds=0,deopts=0,throws=0;void* receiver=nullptr;void* actual_listener=nullptr;
void Added(void* r,void* l,uint32_t events){assert(r==receiver&&l&&events==0x20);actual_listener=l;++adds;}
void Legacy(void* r,void* l,uint32_t e){Added(r,l,e);}
void Boolean(void* r,void* l,uint32_t e,bool trace){assert(!trace);Added(r,l,e);}
void Typed(void* r,void* l,uint32_t e,abi::arm64::android_common::ListenerType t){assert(t==abi::arm64::android_common::ListenerType::kRegularListener);Added(r,l,e);}
void Deopt(void* r,const char* key){assert(r==receiver&&!std::strcmp(key,"artdcevm-final-bytecode-guard"));++deopts;}
struct Engine:HookEngine {
 int family;bool available=false;explicit Engine(int f):family(f){}
 bool Initialize(std::string*)override{return true;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}
 bool Remove(void*,std::string*)override{return false;}
 void* FindSymbol(const char*,const char* name)override {
  if(!available)return nullptr;
  if(!std::strcmp(name,"_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc"))return reinterpret_cast<void*>(Deopt);
  if(family==3&&!std::strcmp(name,"_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj"))return reinterpret_cast<void*>(Legacy);
  if(family==4&&!std::strcmp(name,"_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb"))return reinterpret_cast<void*>(Boolean);
  if(family==5&&!std::strcmp(name,abi::arm64::android_common::kTypedAddListener))return reinterpret_cast<void*>(Typed);
  return nullptr;
 }
};
bool Field(void* p,void*,uint32_t* flags,void** owner){*flags=0x10;*owner=p;return true;}
bool Method(void*,void* method,void** owner){*owner=method;return true;}
bool Equivalent(void*,void* a,void* b){return a==b;}
void Raise(void*,void*,const char* type,const char*){assert(!std::strcmp(type,"Ljava/lang/IllegalAccessError;"));++throws;}
int main(int argc,char** argv){assert(argc==2);Engine engine(argv[1][0]-'0');int r=0,thread=0,method=0,field=0;receiver=&r;
 Guard guard({receiver,Field,Method,Equivalent,Raise});std::string error;
 assert(!BindListener(engine,receiver,guard,&error));engine.available=true;
 assert(BindListener(engine,receiver,guard,&error));assert(!ListenerProven());
 assert(InstallListenerAtExclusiveBoundary(&error));assert(adds==1&&deopts==1&&actual_listener);assert(!ListenerProven());
 assert(InstallListenerAtExclusiveBoundary(&error)&&adds==1&&deopts==1);
 assert(guard.BeginProof({&thread,&method,&field}));NotifyWrittenForTesting(false,&thread,&method,&field);
 assert(guard.EndProof(true,true)&&ListenerProven());
 NotifyWrittenForTesting(true,&thread,&method,&field);assert(throws==2);
 RetainInterpretationAtCommit();assert(deopts==2);
 int other=0;assert(!BindListener(engine,&other,guard,&error));
 std::cout<<"Named final FieldWritten listener family "<<engine.family<<": exact event mask, both callbacks, behavioral proof and commit interpretation route passed\n";
}

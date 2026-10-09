#include <dlfcn.h>
#include <jni.h>
#include <string>
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
JavaVM actual_vm{};int image,foreign,witness,loads=0,initializations=0,hooked=0;void* event_handler=nullptr;
bool plain_success=false,wrong_owner=false,wrong_vm=false,missing_loader=false,native_load_fail=false,initialize_during_hook=false;
const char* dl_error="app namespace denied libopenjdkjvmti.so";
void* Plain(const char* name,int flags){assert(!std::strcmp(name,"libopenjdkjvmti.so")&&flags==(RTLD_LAZY|RTLD_LOCAL));return plain_success?&image:nullptr;}
char* Error(){return const_cast<char*>(dl_error);}
void* Symbol(void*,const char*){return nullptr;}
int Owner(const void*,Dl_info* info){info->dli_fname=wrong_owner?"/data/app/libother.so":"/apex/com.android.art/lib64/libart.so";return 1;}
jint Created(JavaVM** result,jsize size,jsize* count){assert(size==1);*count=1;*result=wrong_vm?reinterpret_cast<JavaVM*>(&foreign):&actual_vm;return JNI_OK;}
void* SourceLoad(const char* name,int flags,const void* carrier){assert(!std::strcmp(name,"libopenjdkjvmti.so")&&flags==(RTLD_LAZY|RTLD_LOCAL)&&carrier==reinterpret_cast<void*>(Created));++loads;return native_load_fail?nullptr:&image;}
}
#define dlopen Plain
#define dlerror Error
#define dlsym Symbol
#define dladdr Owner
#include "platform/common/redefinition_gate.cc"
#undef dlopen
#undef dlerror
#undef dlsym
#undef dladdr
using namespace artdcevm;
namespace {
jvmtiError Direct(void*,void*,void*,const void*,int32_t,void*){return JVMTI_ERROR_NONE;}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(!std::strcmp(name,"JNI_GetCreatedJavaVMs"))return reinterpret_cast<void*>(Created);
  if(!std::strcmp(name,"__loader_dlopen"))return missing_loader?nullptr:reinterpret_cast<void*>(SourceLoad);
  if(std::strstr(name,"gEventHandler"))return &event_handler;
  if(std::strstr(name,"RedefineClassesDirect"))return reinterpret_cast<void*>(Direct);
  assert(false);return nullptr;
 }
 void* Install(void* target,void*,void** original,std::string*)override{assert(target==reinterpret_cast<void*>(Direct));++hooked;*original=target;if(initialize_during_hook){event_handler=&witness;++initializations;}return this;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
}
int main(int argc,char** argv){
 Hooks hooks;std::string report;using namespace redefinition_gate;
 if(argc>1&&std::string(argv[1])=="race"){
  initialize_during_hook=true;assert(MapPluginForStartup(hooks,&actual_vm,&report)==&image);assert(!InstallFirstLive(hooks,&actual_vm,&report)&&!Ready(&actual_vm)&&hooked==1&&initializations==1);
  assert(report.find("historical")!=std::string::npos);std::cout<<"Actual startup mapper+gate: racing plugin initializer cannot grant readiness PASS\n";return 0;
 }
 if(argc>1&&std::string(argv[1])=="old"){
  event_handler=&witness;initializations=1;assert(MapPluginForStartup(hooks,&actual_vm,&report)==&image);assert(!InstallFirstLive(hooks,&actual_vm,&report)&&!Ready(&actual_vm)&&hooked==1);
  assert(event_handler==&witness);std::cout<<"Actual startup mapper+gate: already initialized plugin remains refused without resetting witness PASS\n";return 0;
 }
 plain_success=true;assert(MapPluginForStartup(hooks,&actual_vm,&report)==&image&&loads==0&&initializations==0);plain_success=false;
 wrong_owner=true;assert(!MapPluginForStartup(hooks,&actual_vm,&report)&&loads==0);wrong_owner=false;
 wrong_vm=true;assert(!MapPluginForStartup(hooks,&actual_vm,&report)&&loads==0);wrong_vm=false;
 missing_loader=true;assert(!MapPluginForStartup(hooks,&actual_vm,&report)&&loads==0);missing_loader=false;
 native_load_fail=true;assert(!MapPluginForStartup(hooks,&actual_vm,&report)&&loads==1);native_load_fail=false;
 assert(MapPluginForStartup(hooks,&actual_vm,&report)==&image&&loads==2&&initializations==0&&event_handler==nullptr);
 assert(report.find("app namespace denied")!=std::string::npos&&report.find("initializer not called")!=std::string::npos);
 assert(InstallFirstLive(hooks,&actual_vm,&report)&&Ready(&actual_vm)&&hooked==1&&initializations==0);
 // ART now initializes its already mapped image; its future Direct callers
 // traverse the gate that was published before any environment could exist.
 event_handler=&witness;++initializations;assert(Ready(&actual_vm));{Lease lease(&actual_vm);assert(lease.Acquired()&&Owned(&actual_vm));}
 std::cout<<"Actual startup mapper+gate: app namespace denial, public VM+ART owner proof, named loader, map-before-init/null-after-hook and retained image PASS\n";
}

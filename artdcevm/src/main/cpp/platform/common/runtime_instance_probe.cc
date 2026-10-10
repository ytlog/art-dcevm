#include "platform/common/runtime_instance_probe.h"
#include "platform/abi/common/suspend_all.h"
#include <mutex>
#include <thread>
#include <system_error>
namespace artdcevm::runtime_instance {
namespace {
// Fixed runtime.h Android11–13 and Android14–17 respectively. Overloads are
// selected by their complete ELF names; every original argument is forwarded.
constexpr char kLegacy[]="_ZN3art7Runtime19AttachCurrentThreadEPKcbP8_jobjectb";
constexpr char kModern[]="_ZN3art7Runtime19AttachCurrentThreadEPKcbP8_jobjectbb";
using Legacy=bool(*)(void*,const char*,bool,jobject,bool);
using Modern=bool(*)(void*,const char*,bool,jobject,bool,bool);
void *legacy_original=nullptr,*modern_original=nullptr;
std::mutex installation;
bool attempted=false,installed=false;
struct Observation {void* receiver=nullptr;bool conflict=false;};
thread_local Observation* active=nullptr;
void Observe(void* receiver){
 if(!active)return;
 if(!receiver||(active->receiver&&active->receiver!=receiver))active->conflict=true;
 else active->receiver=receiver;
}
bool LegacyProxy(void* r,const char* n,bool d,jobject g,bool p){
 Observe(r);return reinterpret_cast<Legacy>(legacy_original)(r,n,d,g,p);
}
bool ModernProxy(void* r,const char* n,bool d,jobject g,bool p,bool c){
 Observe(r);return reinterpret_cast<Modern>(modern_original)(r,n,d,g,p,c);
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error){
 std::lock_guard<std::mutex> guard(installation);
 if(installed)return true;
 if(attempted)return Fail(error,"Runtime attachment observer installation failed; restart process");
 void* legacy=hooks.FindSymbol("libart.so",kLegacy);
 void* modern=hooks.FindSymbol("libart.so",kModern);
 using namespace abi::common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if((!legacy&&!modern)||!construct||!destroy)return Fail(error,"Missing named Runtime attachment observation capability");
 attempted=true;SuspendAll storage{};
 construct(&storage,"artdcevm Runtime attachment observation",false);
 bool ok=true;
 if(legacy)ok=hooks.Install(legacy,reinterpret_cast<void*>(LegacyProxy),&legacy_original,error)&&legacy_original;
 if(modern&&ok)ok=hooks.Install(modern,reinterpret_cast<void*>(ModernProxy),&modern_original,error)&&modern_original;
 destroy(&storage);installed=ok;
 return ok||Fail(error,"Runtime attachment observer installation failed; restart process");
}
}
bool Calibrate(HookEngine& hooks,JavaVM* vm,void** output,std::string* error){
 if(!vm||!output)return Fail(error,"Invalid Runtime attachment calibration caller");
 if(!Install(hooks,error))return false;
 Observation observation;bool attached=false,identity=false,detached=false;
 try {
  // A fresh native thread prevents JNI's already-attached fast path from
  // bypassing the original Runtime call. The observer borrows only worker TLS.
  std::thread worker([&]{
   active=&observation;JNIEnv* env=nullptr;
   JavaVMAttachArgs args{JNI_VERSION_1_6,const_cast<char*>("artdcevm-runtime-proof"),nullptr};
#if defined(__ANDROID__)
   attached=vm->AttachCurrentThread(&env,&args)==JNI_OK;
#else
   attached=vm->AttachCurrentThread(reinterpret_cast<void**>(&env),&args)==JNI_OK;
#endif
   active=nullptr;
   if(attached){
    JavaVM* actual=nullptr;
    identity=env&&!env->ExceptionCheck()&&env->GetJavaVM(&actual)==JNI_OK&&actual==vm;
    detached=vm->DetachCurrentThread()==JNI_OK;
   }
  });worker.join();
 }catch(const std::system_error&){return Fail(error,"Unable to create owned Runtime attachment calibration thread");}
 if(!attached||!identity||!detached||!observation.receiver||observation.conflict)
  return Fail(error,"Public VM attachment did not establish a consistent Runtime receiver");
 auto singleton=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 if(!singleton||!*singleton||*singleton!=observation.receiver)
  return Fail(error,"Observed Runtime receiver disagrees with named singleton");
 *output=observation.receiver;return true;
}
}

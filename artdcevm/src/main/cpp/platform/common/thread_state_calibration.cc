#include "platform/common/thread_state_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <array>
namespace artdcevm::thread_state_calibration {
namespace {
using Current=void* (*)();
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Request {
 JNIEnv* env;JavaVM* vm;jobject carrier;Current current;void* self;
 std::array<thread_suspend_service::Service,2> candidates;
 std::array<bool,2> bound{},native{},runnable{},restored{};
 bool reached=false;
};
bool State(const Request& r,size_t i,uint32_t expected){
 size_t extent=0;uint32_t state=0;
 return r.bound[i]&&r.current()==r.self&&
     method_identity::ReadableMappingExtent(r.self,&extent)&&
     r.candidates[i].State(r.self,extent,&state)&&state==expected&&r.current()==r.self;
}
bool Observe(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<Request*>(raw);if(r.reached)return false;r.reached=true;
 if(scope.env!=r.env||scope.vm!=r.vm||scope.self!=r.self||r.current()!=r.self)return false;
 JavaVM* vm=nullptr;void* fixture=nullptr;
 if(scope.env->GetJavaVM(&vm)!=JNI_OK||vm!=r.vm||scope.env->ExceptionCheck()||
    scope.env->GetObjectRefType(r.carrier)!=JNIGlobalRefType||
    !runnable_executor::DecodeStrongGlobal(scope,r.carrier,&fixture)||!fixture||
    scope.env->ExceptionCheck())return false;
 // Every public JNI/oracle call is complete before reading native state. The
 // decoded fixture does not escape or cross another possible safepoint.
 r.runnable[0]=State(r,0,abi::arm64::android11::kRunnable);
 r.runnable[1]=State(r,1,abi::arm64::android13::kRunnable);
 return r.current()==r.self;
}
}
bool Calibrate(HookEngine& hooks,JNIEnv* env,jobject carrier,
               thread_suspend_service::Service* output,std::string* error){
 if(output)*output={};if(error)error->clear();
 if(!output||!env||!carrier||env->ExceptionCheck())return Fail(error,"Thread-state calibration requires ordinary Native JNI and a strong fixture");
 JavaVM* vm=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||!vm||env->GetObjectRefType(carrier)!=JNIGlobalRefType||env->ExceptionCheck())
  return Fail(error,"Thread-state calibration public VM/strong fixture unavailable");
 auto current=reinterpret_cast<Current>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 if(!current||!runnable_executor::Available())return Fail(error,"Thread-state calibration lacks actual current Thread/Runnable bridge");
 void* self=current();if(!self)return Fail(error,"Thread-state calibration actual current Thread unavailable");
 Request r{env,vm,carrier,current,self,{}};
 const auto finder=[&](const char* name){return hooks.FindSymbol("libart.so",name);};
 r.bound[0]=thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kLegacy16,finder,&r.candidates[0]);
 r.bound[1]=thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,finder,&r.candidates[1]);
 for(size_t i=0;i<2;++i)r.native[i]=State(r,i,abi::arm64::android11::kNative);
 const size_t native_count=size_t(r.native[0])+size_t(r.native[1]);
 if(native_count!=1)return Fail(error,"Thread-state Native word does not select exactly one fixed declaration");
 auto result=runnable_jni::Result::kOperationThrew;
 try{result=runnable_executor::With(env,carrier,Observe,&r);}catch(...){
  // The bridge owns scope restoration; a throwing callback never publishes.
  result=runnable_jni::Result::kOperationThrew;
 }
 // The bridge must have returned completely to this exact original Native
 // thread, even on callback rejection. No JNI occurs after the final reread.
 const bool public_ok=!env->ExceptionCheck();
 for(size_t i=0;i<2;++i)r.restored[i]=State(r,i,abi::arm64::android11::kNative);
 if(result!=runnable_jni::Result::kSuccess||!r.reached||!public_ok||current()!=self)
  return Fail(error,"Thread-state Runnable bridge/context failed or left an exception");
 size_t matches=0,selected=0;
 for(size_t i=0;i<2;++i)if(r.native[i]&&r.runnable[i]&&r.restored[i]){++matches;selected=i;}
 if(matches!=1||size_t(r.restored[0])+size_t(r.restored[1])!=1)
  return Fail(error,"Thread-state Native/Runnable/restoration word proof failed or was ambiguous");
 *output=r.candidates[selected];return true;
}
}

#include "platform/common/native_pending_exception.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
namespace artdcevm::native_pending_exception {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
struct Local {JNIEnv*env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
}
bool Service::ReadCandidate(void*self,bool*out)const noexcept{
 if(!out||!self||!current_||current_()!=self||!offset_||
    reinterpret_cast<uintptr_t>(self)%alignof(void*)||
    offset_>UINTPTR_MAX-sizeof(void*)||reinterpret_cast<uintptr_t>(self)>UINTPTR_MAX-offset_-sizeof(void*))return false;
 uintptr_t exception=0;
 std::memcpy(&exception,static_cast<const unsigned char*>(self)+offset_,sizeof(exception));
 if(current_()!=self)return false;*out=exception!=0;return true;
}
bool Service::Read(void*self,bool*out)const noexcept{return ready_&&ReadCandidate(self,out);}
bool Service::Calibrate(HookEngine&hooks,JNIEnv*env,JavaVM*expected_vm,
 const modern_field_storage::Service&storage,const source_handle_roots::Service&roots,std::string*error){
 JavaVM*vm=nullptr;
 if(!env||env->ExceptionCheck()||!expected_vm||env->GetJavaVM(&vm)!=JNI_OK||vm!=expected_vm||
    !storage.Calibrated()||!roots.Calibrated())return Fail(error,"pending-exception-independent-native-dependencies-unavailable");
 Service candidate;
 candidate.current_=reinterpret_cast<abi::arm64::android11::CurrentThread>(
  hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 constexpr size_t relative=offsetof(abi::arm64::android_common::HandleThreadPrefix,exception);
 const auto prefix=storage.CardTableOffset();
 if(!candidate.current_||!prefix||prefix>SIZE_MAX-relative-sizeof(void*))
  return Fail(error,"pending-exception-fixed-TLS-prefix-unavailable");
 candidate.offset_=prefix+relative;
 if(candidate.offset_%alignof(void*))return Fail(error,"pending-exception-fixed-slot-alignment-differs");
 void*self=candidate.current_();const runnable_jni::Context actual{self,env,vm};
 size_t extent=0;
 if(!roots.OrdinaryNative(actual)||!method_identity::ReadableMappingExtent(self,&extent)||
    extent<candidate.offset_+sizeof(void*))return Fail(error,"pending-exception-owning-native-thread-or-extent-unavailable");
 bool pending=true;
 if(!candidate.ReadCandidate(self,&pending)||pending||env->ExceptionCheck())
  return Fail(error,"pending-exception-public-empty-oracle-differs");
 Local type{env,env->FindClass("java/lang/RuntimeException")};
 if(!type.value||env->ExceptionCheck())return false;
 auto constructor=env->GetMethodID(static_cast<jclass>(type.value),"<init>","()V");
 if(!constructor||env->ExceptionCheck())return false;
 Local throwable{env,env->NewObject(static_cast<jclass>(type.value),constructor)};
 if(!throwable.value||env->ExceptionCheck())return false; // Preserve construction failures.
 if(env->Throw(static_cast<jthrowable>(throwable.value))!=JNI_OK)return false;
 // Throw succeeded: only this deliberate public calibration exception is ours
 // to clear. No executor, JNI root decode or managed dereference runs while set.
 const bool public_pending=env->ExceptionCheck();
 const bool set=candidate.ReadCandidate(self,&pending)&&pending;
 Local observed{env,env->ExceptionOccurred()};
 if(!observed.value)return Fail(error,"pending-exception-public-Throwable-oracle-unavailable");
 env->ExceptionClear();
 const bool own_exception=env->IsSameObject(observed.value,throwable.value)&&!env->ExceptionCheck();
 if(!own_exception){
  // An existing instrumentation callback may replace the deliberate exception.
  // Preserve that exception instead of clearing someone else's failure.
  if(!env->ExceptionCheck())env->Throw(static_cast<jthrowable>(observed.value));
  return Fail(error,"pending-exception-public-Throwable-was-replaced");
 }
 const bool clear=candidate.ReadCandidate(self,&pending)&&!pending&&!env->ExceptionCheck();
 if(!public_pending||!set||!clear||!roots.OrdinaryNative(actual))
  return Fail(error,"pending-exception-public-Throw-clear-oracles-differ");
 if(ready_&&(current_!=candidate.current_||offset_!=candidate.offset_))
  return Fail(error,"pending-exception-existing-source-declaration-differs");
 if(ready_)return true; // Published readers remain immutable on later preparation.
 candidate.ready_=true;*this=candidate;return true;
}
}

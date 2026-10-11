#include "platform/common/production_bootstrap.h"
#include "platform/common/contextual_field_id.h"
#include "platform/common/production_source_calibration.h"

namespace artdcevm::production_bootstrap {
std::unique_ptr<Engine> Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 if(!env||!ti||!fixture){*error="Private evolution preparation requires current JNI/JVMTI and the SDK fixture";return nullptr;}
 if(env->ExceptionCheck()){*error="Private evolution preparation preserves the pending JNI exception";return nullptr;}
 if(!hooks.Initialize(error))return nullptr;
 auto*source=SourceCalibration::Prepare(hooks,env,ti,fixture,error);
 if(!source){if(error->empty())*error="Private evolution source calibration did not complete";return nullptr;}
 // Source calibration is now reachable, but its samples do not replace the
 // full union/stack/native-boundary and permanent receiver permissions. Never
 // publish a partial Engine whose Apply could bypass those requirements.
 *error="Private evolution source calibration completed; transaction construction still requires complete stopped-stack/native-boundary proof and permanent receiver protection";
 return nullptr;
}
}

// Ordinary JNI proof used before a fresh retained ID is exercised. In particular,
// the contextual service rejects an opaque-ID collision with a live retired alias.
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_internal_ProductionBootstrap_verifyLiveFieldId(
    JNIEnv* env,jclass,jobject field){
 std::string error;
 jfieldID id=field?env->FromReflectedField(field):nullptr;
 auto proof=artdcevm::contextual_field_id::Prepare(env,field,id,&error);
 const bool ready=bool(proof);
 artdcevm::contextual_field_id::Release(env,&proof);
 if(env->ExceptionCheck())return nullptr;
 return ready?nullptr:env->NewStringUTF(error.empty()?"Fresh field ID identity could not be proven":error.c_str());
}

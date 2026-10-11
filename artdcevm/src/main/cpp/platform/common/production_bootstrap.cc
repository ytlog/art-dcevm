#include "platform/common/production_bootstrap.h"
#include "platform/common/contextual_field_id.h"

namespace artdcevm::production_bootstrap {
std::unique_ptr<Engine> Prepare(JNIEnv*,jvmtiEnv*,jobject,std::string* error){
 // The retired private bootstrap authorized transactions through independent
 // JIT/compiled-frame episodes and the old source RunState. Those owners have
 // been removed. Ordinary metadata or a runtime receiver match cannot replace
 // transaction-wide execution safety, so do not construct a partial engine or
 // run any calibration/redefinition before rejecting private initialization.
 if(error)*error="Private evolution unavailable: transaction callgraph bootstrap is not integrated after removal of legacy JIT/AOT, compiled-frame and source lifecycle owners; public JVMTI body/additive operations remain available";
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

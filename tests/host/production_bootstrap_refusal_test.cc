#include "platform/common/production_bootstrap.h"
#include "platform/common/production_source_calibration.h"
#include "platform/common/contextual_field_id.h"
#include <cassert>
#include <iostream>
namespace {
bool pending=false,source_success=false;size_t source_calls=0;
artdcevm::HookEngine*expected_hooks=nullptr;
jboolean ExceptionCheck(JNIEnv*){return pending?JNI_TRUE:JNI_FALSE;}
class Hooks final:public artdcevm::HookEngine {
 public:
 bool available=true;size_t initializations=0;
 bool Initialize(std::string*error)override{++initializations;if(!available)*error="named hook mechanism unavailable";return available;}
 void*FindSymbol(const char*,const char*)override{std::abort();}
 void*Install(void*,void*,void**,std::string*)override{std::abort();}
 bool Remove(void*,std::string*)override{std::abort();}
};
}
namespace artdcevm::contextual_field_id {
std::shared_ptr<Proof> Prepare(JNIEnv*,jobject,jfieldID,std::string*){std::abort();}
void Release(JNIEnv*,std::shared_ptr<Proof>*){std::abort();}
}
namespace artdcevm::production_bootstrap {
// Integration boundary only. This stub proves ordering/error propagation,
// never ART scope, calibration success, mutation or transaction readiness.
SourceCalibration*SourceCalibration::Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 assert(&hooks==expected_hooks&&env&&ti&&fixture&&!pending);++source_calls;
 if(!source_success){*error="source ownership calibration refused";return nullptr;}
 return reinterpret_cast<SourceCalibration*>(uintptr_t(1)); // Never dereferenced.
}
}
int main(){
 using artdcevm::production_bootstrap::Prepare;
 Hooks hooks;expected_hooks=&hooks;JNINativeInterface_ table{};table.ExceptionCheck=ExceptionCheck;
 JNIEnv env{&table};auto*ti=reinterpret_cast<jvmtiEnv*>(uintptr_t(3));
 auto fixture=reinterpret_cast<jobject>(uintptr_t(5));std::string error;
 assert(!Prepare(hooks,nullptr,ti,fixture,&error));
 assert(!Prepare(hooks,&env,nullptr,fixture,nullptr));
 assert(!Prepare(hooks,&env,ti,nullptr,&error));
 assert(!source_calls&&!hooks.initializations);
 pending=true;assert(!Prepare(hooks,&env,ti,fixture,&error));
 assert(error.find("pending JNI exception")!=std::string::npos&&!source_calls&&!hooks.initializations&&pending);
 pending=false;hooks.available=false;assert(!Prepare(hooks,&env,ti,fixture,&error));
 assert(error=="named hook mechanism unavailable"&&!source_calls&&hooks.initializations==1);
 hooks.available=true;assert(!Prepare(hooks,&env,ti,fixture,&error));
 assert(error=="source ownership calibration refused"&&source_calls==1);
 source_success=true;assert(!Prepare(hooks,&env,ti,fixture,&error));
 assert(source_calls==2&&error.find("complete stopped-stack/native-boundary proof")!=std::string::npos&&
        error.find("permanent receiver protection")!=std::string::npos);
 assert(!Prepare(hooks,&env,ti,fixture,nullptr));assert(source_calls==3);
 std::cout<<"Production preparation boundary: pending preservation, shared hooks, source refusal, no partial Engine PASS\n";
}

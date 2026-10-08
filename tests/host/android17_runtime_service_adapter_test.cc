#include "platform/android17/runtime_services.h"
#include "platform/common/runtime_service_calibration.h"
#include <cassert>
#include <iostream>
using namespace artdcevm;
namespace {
struct Hooks final:HookEngine {bool Initialize(std::string*)override{assert(false);return false;}void* FindSymbol(const char*,const char*)override{assert(false);return nullptr;}void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
Hooks hooks;JNIEnv* expected_env=reinterpret_cast<JNIEnv*>(1);int calls=0;bool success=true;
}
namespace artdcevm::runtime_services {
bool CalibrateRuntimeServices(HookEngine& h,JNIEnv* env,Snapshot* output,std::string* error){
 assert(&h==&hooks&&env==expected_env);++calls;
 if(!success||!output){if(error)*error="shared observer refusal";return false;}
 output->runtime=reinterpret_cast<void*>(5);output->instrumentation=reinterpret_cast<void*>(6);
 output->receivers.jni_id_manager=reinterpret_cast<void*>(7);if(error)error->clear();return true;
}
}
int main(){android17::RuntimeServices snapshot;std::string error;
 assert(android17::CalibrateRuntimeServices(hooks,expected_env,&snapshot,&error));
 assert(calls==1&&snapshot.runtime==reinterpret_cast<void*>(5)&&snapshot.instrumentation==reinterpret_cast<void*>(6)&&snapshot.receivers.jni_id_manager==reinterpret_cast<void*>(7)&&error.empty());
 success=false;assert(!android17::CalibrateRuntimeServices(hooks,expected_env,&snapshot,&error)&&calls==2&&error=="shared observer refusal"&&snapshot.runtime==reinterpret_cast<void*>(5));
 assert(!android17::CalibrateRuntimeServices(hooks,expected_env,nullptr,&error)&&calls==3);
 std::cout<<"Android17 runtime adapter: exact shared snapshot/arguments/error propagation, no second hook owner passed\n";
}

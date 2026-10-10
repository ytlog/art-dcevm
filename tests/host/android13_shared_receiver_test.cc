#include "platform/android13/runtime_receiver_probe.h"
#include "platform/common/runtime_receiver_probe.h"
#include "platform/android13/native_deletion_capabilities.h"
#include "platform/common/runtime_service_calibration.h"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace artdcevm;
namespace {
class Hooks final:public HookEngine {
 public:
 bool Initialize(std::string*)override{assert(false);return false;}
 void* FindSymbol(const char*,const char*)override{assert(false);return nullptr;}
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
int calls=0;
bool calibrated=true;
runtime_services::Snapshot observed;
bool handler_ready=false;
void* handler=nullptr;
}
namespace artdcevm::runtime_services {
// Boundary double: tests the compiled source adapter, not the shared observer.
// Real public-call observation remains covered by the common owner's tests.
bool CalibrateRuntimeServices(HookEngine&,JNIEnv*,Snapshot* output,std::string* error) {
 ++calls;
 if(!calibrated){if(error)*error="owned observation rejected";return false;}
 *output=observed;return true;
}
}
namespace artdcevm::runtime_receiver {
bool SetFindClassHandler(void* value,void** original) {
 if(!handler_ready)return false;
 handler=value;if(original)*original=reinterpret_cast<void*>(0x90);return true;
}
}
int main() {
 Hooks hooks;JNINativeInterface_ table{};JNIEnv env{&table};std::string error;
 const android13::RuntimeReceivers sentinel{reinterpret_cast<void*>(0xaa),reinterpret_cast<void*>(0xbb),nullptr,reinterpret_cast<void*>(0xcc),reinterpret_cast<void*>(0xdd)};
 auto unchanged=[&](const android13::RuntimeReceivers& result){
  assert(result.class_linker==sentinel.class_linker&&result.jni_id_manager==sentinel.jni_id_manager&&result.java_vm==sentinel.java_vm&&result.thread_list==sentinel.thread_list&&result.instrumentation==sentinel.instrumentation);
 };
 android13::RuntimeReceivers output=sentinel;
 assert(!android13::CalibrateRuntimeReceivers(hooks,nullptr,&output,&error));unchanged(output);
 assert(!android13::CalibrateRuntimeReceivers(hooks,&env,nullptr,&error)&&calls==0);
 observed.runtime=reinterpret_cast<void*>(0x10);observed.instrumentation=reinterpret_cast<void*>(0x20);
 observed.receivers={reinterpret_cast<void*>(0x30),reinterpret_cast<void*>(0x40),nullptr,nullptr,reinterpret_cast<void*>(0x50),nullptr,reinterpret_cast<void*>(0x60)};
 const auto complete=observed;
 calibrated=false;
 assert(!android13::CalibrateRuntimeReceivers(hooks,&env,&output,&error));
 assert(error=="owned observation rejected");unchanged(output);
 calibrated=true;
 // No partial publication if any independently observed receiver is missing.
 for(int missing=0;missing<5;++missing) {
  observed=complete;
  if(missing==0)observed.receivers.class_linker=nullptr;
  if(missing==1)observed.receivers.jni_id_manager=nullptr;
  if(missing==2)observed.receivers.java_vm=nullptr;
  if(missing==3)observed.receivers.thread_list=nullptr;
  if(missing==4)observed.instrumentation=nullptr;
  assert(!android13::CalibrateRuntimeReceivers(hooks,&env,&output,&error)&&!error.empty());unchanged(output);
 }
 observed=complete;
 // Bootstrapping and source initialization may calibrate repeatedly without
 // reinstalling any callee; Hooks rejects every direct source installation.
 for(int invocation=0;invocation<2;++invocation) {
  assert(android13::CalibrateRuntimeReceivers(hooks,&env,&output,&error)&&error.empty());
  assert(output.class_linker==complete.receivers.class_linker&&output.jni_id_manager==complete.receivers.jni_id_manager);
  assert(output.java_vm==complete.receivers.java_vm&&output.thread_list==complete.receivers.thread_list&&output.instrumentation==complete.instrumentation);
 }
 assert(!runtime_receiver::SetFindClassHandler(reinterpret_cast<void*>(0x80),nullptr));
 handler_ready=true;void* original=nullptr;
 assert(runtime_receiver::SetFindClassHandler(reinterpret_cast<void*>(0x80),&original)&&handler==reinterpret_cast<void*>(0x80)&&original==reinterpret_cast<void*>(0x90));
 assert(runtime_receiver::SetFindClassHandler(nullptr,nullptr)&&handler==nullptr);
 // Formal capability consumers require both native and field deletion keys.
 std::ostringstream unavailable,available;
 android13::WriteNativeDeletionCapabilities(unavailable,false);
 android13::WriteNativeDeletionCapabilities(available,true);
 assert(unavailable.str()==",\"nativeDelete\":false,\"fieldDeletion\":false");
 assert(available.str()==",\"nativeDelete\":true,\"fieldDeletion\":true");
 std::cout<<"Android13 shared receiver owner and deletion contract: passed\n";
}

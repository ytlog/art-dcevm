#include "platform/android17/runtime_receiver_probe.h"
#include "platform/common/runtime_receiver_probe.h"
namespace artdcevm::android17 {
bool SetRuntimeFindClassHandler(void* handler,void** original) {
  return runtime_receiver::SetLengthFindClassHandler(handler,original);
}
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,std::string* error) {
  if(!output){if(error)*error="Missing Runtime receiver output";return false;}
  runtime_receiver::Receivers observed;
  if(!runtime_receiver::Calibrate(hooks,env,&observed,error))return false;
  *output={observed.class_linker,observed.jni_id_manager,observed.java_vm,observed.thread_list};
  return true;
}
}

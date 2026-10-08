#include "platform/android13/runtime_receiver_probe.h"
#include "platform/common/runtime_receiver_probe.h"
#include "platform/common/runtime_service_calibration.h"
namespace artdcevm::android13 {
bool SetRuntimeFindClassHandler(void* handler,void** original) {
  return runtime_receiver::SetFindClassHandler(handler,original);
}
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env || !output) {
    if(error)*error="Invalid Runtime receiver calibration caller";
    return false;
  }
  // Production bootstrap and the source backend share one process-lifetime
  // listener observer. This owned calibration observes again through the same
  // original callee; it never installs a second Hook or trusts an SDK/profile.
  runtime_services::Snapshot snapshot;
  if(!runtime_services::CalibrateRuntimeServices(hooks,env,&snapshot,error))return false;
  const auto& observed=snapshot.receivers;
  if(!observed.class_linker || !observed.jni_id_manager || !observed.java_vm ||
     !observed.thread_list || !snapshot.instrumentation) {
    if(error)*error="Shared Runtime calibration did not establish all source receivers";
    return false;
  }
  // Heap/JIT or other fixed-prefix candidates are not published here. Source
  // initialization still performs its complete independent Runtime/layout proof.
  *output={observed.class_linker,observed.jni_id_manager,
           static_cast<JavaVM*>(observed.java_vm),observed.thread_list,
           snapshot.instrumentation};
  return true;
}
}

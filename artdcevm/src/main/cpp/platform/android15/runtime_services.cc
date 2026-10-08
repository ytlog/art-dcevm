#include "platform/android15/runtime_services.h"
#include "platform/common/runtime_service_calibration.h"
namespace artdcevm::android15 {
bool CalibrateRuntimeServices(HookEngine& hooks,JNIEnv* env,RuntimeServices* output,
    std::string* error) {
  return runtime_services::CalibrateRuntimeServices(hooks,env,output,error);
}
}

#include "platform/android17/runtime_services.h"
#include "platform/common/runtime_service_calibration.h"
namespace artdcevm::android17 {
bool CalibrateRuntimeServices(HookEngine& hooks,JNIEnv* env,RuntimeServices* output,
                              std::string* error) {
  // One observed receiver owner and source-declared listener ABI per process.
  // The shared calibration independently selects its Runtime declaration.
  return runtime_services::CalibrateRuntimeServices(hooks,env,output,error);
}
}

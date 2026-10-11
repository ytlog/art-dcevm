#include "platform/backend_factory.h"
#include "platform/jvmti/public_backend.h"
#include "platform/common/capability_backend.h"
#include "hooks/shadow_hook_engine.h"
namespace artdcevm {
std::unique_ptr<RuntimeBackend> CreateBackend() {
  auto public_backend=std::make_unique<jvmti_backend::PublicBackend>();
  return std::make_unique<CapabilityBackend>(std::move(public_backend),
      [](JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
        return production_bootstrap::Prepare(ProcessHookEngine(),env,ti,fixture,error);
      });
}
}

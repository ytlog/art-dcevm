#include "platform/backend_factory.h"
#include "platform/jvmti/public_backend.h"
#if defined(ARTDCEVM_ARM64)
#include "platform/android13/android13_backend.h"
#include "platform/common/capability_backend.h"
#endif
namespace artdcevm {
std::unique_ptr<RuntimeBackend> CreateBackend() {
  auto public_backend=std::make_unique<jvmti_backend::PublicBackend>();
#if defined(ARTDCEVM_ARM64)
  return std::make_unique<CapabilityBackend>(std::move(public_backend),
      [] {return std::make_unique<android13::Android13Backend>();},
      production_bootstrap::Prepare);
#else
  return public_backend;
#endif
}
}

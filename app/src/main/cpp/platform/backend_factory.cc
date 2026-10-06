#include "platform/backend_factory.h"
#include "platform/jvmti/public_backend.h"
#if defined(ARTDCEVM_ARM64)
#include "platform/android13/android13_backend.h"
#endif
#include <cstring>
#include <cstdlib>
#include <sys/system_properties.h>
namespace artdcevm {
std::unique_ptr<RuntimeBackend> CreateBackend() {
  char sdk[PROP_VALUE_MAX]{};
  __system_property_get("ro.build.version.sdk", sdk);
  if (std::atoi(sdk) < 33) return nullptr;
#if defined(ARTDCEVM_ARM64)
  if (std::strcmp(sdk, "33") == 0) return std::make_unique<android13::Android13Backend>();
#endif
  return std::make_unique<jvmti_backend::PublicBackend>();
}
}

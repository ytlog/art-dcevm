#include "platform/backend_factory.h"
#include "platform/android13/android13_backend.h"
#include <cstring>
#include <sys/system_properties.h>
namespace artdcevm {
std::unique_ptr<RuntimeBackend> CreateBackend() {
  char sdk[PROP_VALUE_MAX]{};
  __system_property_get("ro.build.version.sdk", sdk);
  if (std::strcmp(sdk, "33") == 0) return std::make_unique<android13::Android13Backend>();
  return nullptr;
}
}

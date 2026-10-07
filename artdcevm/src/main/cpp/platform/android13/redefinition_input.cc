#include "platform/android13/redefinition_input.h"
#include <cstring>
#include <limits>
#include <jvmti.h>
namespace artdcevm::android13 {
int ReadRedefinitionInput(void* definition, redefinition_abi::ClassRedefinition* metadata,
                          evolution::ClassSchema* replacement, std::string* error) {
  std::memcpy(metadata, definition, sizeof(*metadata));
  if (!metadata->klass || !metadata->dex_file) {
    *error = "Missing class or replacement DEX";
    return JVMTI_ERROR_INTERNAL;
  }
  redefinition_abi::DexFilePrefix dex{};
  std::memcpy(&dex, metadata->dex_file, sizeof(dex));
  if (!dex.begin || dex.size < 0x70 || dex.size > static_cast<size_t>(std::numeric_limits<jint>::max())) {
    *error = "Invalid replacement DEX range";
    return JVMTI_ERROR_INTERNAL;
  }
  uint32_t class_count;
  std::memcpy(&class_count, dex.begin + 96, sizeof(class_count));
  if (class_count != 1) {
    *error = "Expected one class definition in replacement DEX";
    return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  }
  const std::vector<unsigned char> bytes(dex.begin, dex.begin + dex.size);
  return evolution::ReadDexDeclarations(bytes, replacement, error)
      ? JVMTI_ERROR_NONE : JVMTI_ERROR_INVALID_CLASS_FORMAT;
}
}

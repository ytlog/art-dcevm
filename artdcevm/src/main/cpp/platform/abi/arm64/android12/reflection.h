#pragma once
#include "platform/abi/arm64/object.h"
namespace artdcevm::abi::arm64::android12 {
// Fixed AccessibleObject/Field/Executable source declaration, with managed
// subclass tail-padding reuse flattened explicitly. Match/calibrate before use.
#pragma pack(push, 4)
struct ReflectedField {
  ObjectPrefix object;
  uint8_t accessible;
  uint8_t padding[3];
  uint32_t declaring_class;
  uint32_t type;
  uint32_t access_flags;
  uint32_t art_field_index;
  uint32_t offset;
};
struct Executable {
  ObjectPrefix object;
  uint8_t accessible;
  uint8_t has_real_parameter_data;
  uint8_t padding[2];
  uint32_t declaring_class;
  uint32_t declaring_class_of_overridden_method;
  uint32_t parameters;
  uint64_t art_method;
  uint32_t access_flags;
  uint32_t dex_method_index;
};
#pragma pack(pop)
}

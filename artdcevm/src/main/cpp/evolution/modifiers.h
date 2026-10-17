#pragma once
#include "evolution/class_schema.h"

namespace artdcevm::evolution {
// Raw DEX access flags mapped to the JVMTI Get*Modifiers view
// (openjdkjvmti ti_class.cc, ti_field.cc, ti_method.cc).
inline uint32_t ClassModifiers(uint32_t dex_access) { return dex_access & 0xffff; }
inline uint32_t FieldModifiers(uint32_t dex_access) { return dex_access & 0xffff; }
inline uint32_t MethodModifiers(uint32_t dex_access) {
  uint32_t modifiers = dex_access;
  if (modifiers & kAccAbstract) modifiers &= ~kAccNative;
  modifiers &= ~kAccSynchronized;
  if (modifiers & kAccDeclaredSynchronized) modifiers |= kAccSynchronized;
  return modifiers & 0xffff;
}
}  // namespace artdcevm::evolution

#pragma once
#include <cstdint>
namespace artdcevm::jvmti_field_flags {
// Fixed Android 11/12 openjdkjvmti/ti_field.cc FieldUtil::GetFieldModifiers
// and Field.getModifiers expose only the low 16 access bits. High runtime
// bits never acquire a method-only DeclaredSynchronized interpretation.
constexpr uint32_t Public(uint32_t raw) noexcept {return raw&0xffffu;}
}

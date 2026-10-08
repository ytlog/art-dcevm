#pragma once
#include <cstdint>
namespace artdcevm::deoptimize_if_needed_abi {
// Android 14/15 fixed Instrumentation::DeoptimizeIfNeeded declaration.
union JValue {uint8_t z;int8_t b;uint16_t c;int16_t s;int32_t i;int64_t j;float f;double d;void* l;};
enum class MethodType {kKeepDexPc=0,kDefault=1};
static_assert(sizeof(JValue)==8&&alignof(JValue)==8);
using ReturnsVoid = void (*)(void*,void*,void**,MethodType,JValue,bool);
}  // namespace artdcevm::deoptimize_if_needed_abi

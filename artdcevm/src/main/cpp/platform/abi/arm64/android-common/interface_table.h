#pragma once
#include "platform/abi/arm64/object.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64::android_common {
// Fixed mirror/array.h and mirror/iftable.h declarations: IfTable inherits
// ObjectArray<Object>, with an interface and method array in each slot pair.
// Public transitive interface identities must independently calibrate this
// declaration before the common reader uses it on a runtime.
struct ObjectArrayPrefix {
  ObjectPrefix object;
  int32_t length;
};
static_assert(sizeof(ObjectArrayPrefix)==12);
static_assert(offsetof(ObjectArrayPrefix,length)==8);
inline constexpr size_t kIfTableLength=offsetof(ObjectArrayPrefix,length);
inline constexpr size_t kIfTableData=sizeof(ObjectArrayPrefix);
inline constexpr int32_t kIfTablePairWidth=2;
}

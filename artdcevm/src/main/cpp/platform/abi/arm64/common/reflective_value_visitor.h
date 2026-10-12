#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>

// runtime/reflective_value_visitor.h, identical in android-11.0.0_r1 through
// android-17.0.0_r1.
namespace artdcevm::abi::arm64 {
struct ArtField;
struct ArtMethod;

// ReflectionSourceInfo: vptr (destructor, Describe), then ReflectionSourceType.
struct ReflectionSourceInfo {
  const void* vtable;
  int32_t type;
};
// JNIIdReflectiveSourceInfo<jmethodID>: the visited ID follows the base.
struct JniMethodIdSourceInfo {
  ReflectionSourceInfo base;
  jmethodID id;
};
static_assert(offsetof(JniMethodIdSourceInfo, id) == 16);
// ReflectionSourceType.
inline constexpr int32_t kSourceJniMethodId = 7;
inline constexpr int32_t kSourceDexCacheResolvedMethod = 8;

// Virtual functions in declaration order: destructor, VisitMethod, VisitField.
class ReflectiveValueVisitor {
 public:
  virtual ~ReflectiveValueVisitor() = default;
  virtual ArtMethod* VisitMethod(ArtMethod*, const ReflectionSourceInfo&) = 0;
  virtual ArtField* VisitField(ArtField*, const ReflectionSourceInfo&) = 0;
};
}  // namespace artdcevm::abi::arm64

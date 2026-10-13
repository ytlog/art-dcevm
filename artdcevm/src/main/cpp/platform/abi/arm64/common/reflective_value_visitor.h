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
// JniIdReflectiveSourceInfo<T>: the visited ID follows the base.
struct JniMethodIdSourceInfo {
  ReflectionSourceInfo base;
  jmethodID id;
};
struct JniFieldIdSourceInfo {
  ReflectionSourceInfo base;
  jfieldID id;
};
// HeapReflectiveSourceInfo: the visited heap object (Field, MethodHandle, VarHandle).
struct HeapSourceInfo {
  ReflectionSourceInfo base;
  const void* source;
};
static_assert(offsetof(JniMethodIdSourceInfo, id) == 16 && offsetof(JniFieldIdSourceInfo, id) == 16 &&
              offsetof(HeapSourceInfo, source) == 16);
// ReflectionSourceType.
inline constexpr int32_t kSourceJavaLangReflectField = 2;
inline constexpr int32_t kSourceJavaLangInvokeFieldVarHandle = 4;
inline constexpr int32_t kSourceJniFieldId = 6;
inline constexpr int32_t kSourceJniMethodId = 7;
inline constexpr int32_t kSourceDexCacheResolvedMethod = 8;
inline constexpr int32_t kSourceDexCacheResolvedField = 9;

// Virtual functions in declaration order: destructor, VisitMethod, VisitField.
class ReflectiveValueVisitor {
 public:
  virtual ~ReflectiveValueVisitor() = default;
  virtual ArtMethod* VisitMethod(ArtMethod*, const ReflectionSourceInfo&) = 0;
  virtual ArtField* VisitField(ArtField*, const ReflectionSourceInfo&) = 0;
};
}  // namespace artdcevm::abi::arm64

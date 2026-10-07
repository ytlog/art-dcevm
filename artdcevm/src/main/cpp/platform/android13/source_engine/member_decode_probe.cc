#include "platform/android13/member_decode_abi.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "mirror/field-inl.h"
#include "mirror/executable.h"
#include "scoped_thread_state_change-inl.h"
#include <cstddef>
#include <initializer_list>

namespace art {
struct FieldOffsets {
  static void VerifyDecode() {
    using Fixed = artdcevm::android13::member_decode_abi::ReflectedField;
    static_assert(sizeof(mirror::Field) == sizeof(Fixed));
    static_assert(alignof(mirror::Field) == alignof(Fixed));
    static_assert(offsetof(mirror::Field, type_) == offsetof(Fixed, type));
    static_assert(offsetof(mirror::Field, offset_) == offsetof(Fixed, offset));
    static_assert(offsetof(mirror::Field, declaring_class_) == offsetof(Fixed, declaring_class));
    static_assert(offsetof(mirror::Field, access_flags_) == offsetof(Fixed, access_flags));
    static_assert(offsetof(mirror::Field, art_field_index_) == offsetof(Fixed, art_field_index));
  }
};
struct ExecutableOffsets {
  static void VerifyDecode() {
    using Fixed = artdcevm::android13::member_decode_abi::Executable;
    static_assert(sizeof(mirror::Executable) == sizeof(Fixed));
    static_assert(alignof(mirror::Executable) == alignof(Fixed));
    static_assert(offsetof(mirror::Executable, art_method_) == offsetof(Fixed, art_method));
  }
};
}

// Read-only calibration against the source declaration and original device
// accessors. The enhanced runtime owns field decoding and registry lookup.
extern "C" int ArtDcevmSourceProbeMemberDecode(JNIEnv* jni, jobject calibration,
    void* (*decode_field)(void*), void* (*decode_method)(const void*, jobject)) {
  art::FieldOffsets::VerifyDecode();
  art::ExecutableOffsets::VerifyDecode();
  if (!decode_field || !decode_method || art::kIsDebugBuild || art::kPoisonHeapReferences) return 1;
  jclass klass = jni->GetObjectClass(calibration);
  if (!klass || jni->ExceptionCheck()) return 2;
  const int result = [&]() {
    for (const char* name : {"value", "wide", "counter"}) {
      const bool is_static = name[0] == 'c';
      const char* signature = name[0] == 'w' ? "J" : "I";
      auto id = is_static ? jni->GetStaticFieldID(klass, name, signature) :
          jni->GetFieldID(klass, name, signature);
      if (!id || jni->ExceptionCheck()) return 3;
      auto reflected = jni->ToReflectedField(klass, id, is_static);
      if (!reflected || jni->ExceptionCheck()) return 4;
      bool matches;
      {
        art::ScopedObjectAccess soa(jni);
        auto field = soa.Decode<art::mirror::Field>(reflected);
        matches = decode_field(field.Ptr()) == field->GetArtField();
      }
      jni->DeleteLocalRef(reflected);
      if (!matches) return 5;
    }
    auto id = jni->GetMethodID(klass, "compute", "()I");
    if (!id || jni->ExceptionCheck()) return 6;
    auto reflected = jni->ToReflectedMethod(klass, id, false);
    if (!reflected || jni->ExceptionCheck()) return 7;
    bool matches;
    {
      art::ScopedObjectAccess soa(jni);
      auto executable = soa.Decode<art::mirror::Executable>(reflected);
      matches = decode_method(&soa, reflected) == executable->GetArtMethod();
    }
    jni->DeleteLocalRef(reflected);
    return matches ? 0 : 8;
  }();
  jni->DeleteLocalRef(klass);
  return result;
}

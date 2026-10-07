// Compiled separately against the pinned AOSP platform headers and libc++.
// Only JNI and plain C values cross the NDK/platform C++ ABI boundary.
#include "art_method-inl.h"
#include "art_field-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "runtime.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include "jni/jni_env_ext.h"
#include "class_linker-inl.h"
#include <cstring>

extern "C" __attribute__((visibility("default"))) int ArtDcevmSourceProbe(
    JNIEnv* jni, jobject object) {
  JavaVM* vm = nullptr;
  if (jni->GetJavaVM(&vm) != JNI_OK) return 1;
  auto* thread = art::Thread::Current();
  if (thread == nullptr || thread->GetJniEnv() != jni) return 2;
  if (reinterpret_cast<JavaVM*>(art::Runtime::Current()->GetJavaVM()) != vm) return 3;
  jclass java_class = jni->GetObjectClass(object);
  jmethodID compute = jni->GetMethodID(java_class, "compute", "()I");
  if (compute == nullptr || jni->ExceptionCheck()) { jni->DeleteLocalRef(java_class); return 9; }
  jfieldID value = jni->GetFieldID(java_class, "value", "I");
  jfieldID wide = jni->GetFieldID(java_class, "wide", "J");
  jni->DeleteLocalRef(java_class);
  if (value == nullptr || wide == nullptr || jni->ExceptionCheck()) return 11;
  if (jni->GetIntField(object, value) != 37 ||
      jni->GetLongField(object, wide) != 0x123456789ABCDEFLL) return 14;
  // These accesses are compiled from AOSP declarations. No binary-derived offsets.
  art::ScopedObjectAccess soa(jni);
  auto instance = soa.Decode<art::mirror::Object>(object);
  auto klass = instance->GetClass();
  std::string storage;
  if (std::strcmp(klass->GetDescriptor(&storage), "Lcom/github/ytlog/artdcevm/platform/android13/SourceAbiProbe;") != 0)
    return 4;
  if (klass->NumInstanceFields() != 2 || klass->NumStaticFields() != 1) return 5;
  auto* field = klass->FindDeclaredInstanceField("value", "I");
  if (field == nullptr || field->GetInt(instance) != 37) return 6;
  auto* wide_field = klass->FindDeclaredInstanceField("wide", "J");
  if (wide_field == nullptr || wide_field->GetLong(instance) != 0x123456789ABCDEFLL ||
      wide_field == field ||
      wide_field->GetOffset().Int32Value() == field->GetOffset().Int32Value()) return 15;
  auto* method = klass->FindClassMethod("compute", "()I", art::kRuntimePointerSize);
  if (method == nullptr || method->GetDeclaringClass() != klass) return 7;
  if (method->GetNameView() != "compute" || method->GetSignature().ToString() != "()I")
    return 8;
  if (art::jni::DecodeArtMethod(compute) != method) return 10;
  if (art::jni::DecodeArtField(value) != field) return 12;
  if (art::jni::DecodeArtField(wide) != wide_field) return 16;
  auto* counter = klass->FindDeclaredStaticField("counter", "I");
  if (counter == nullptr || counter->GetInt(klass) != 7) return 13;
  return 0;
}

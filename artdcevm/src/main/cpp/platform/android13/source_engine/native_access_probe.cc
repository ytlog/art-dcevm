#include "platform/android13/native_object_access_abi.h"
#include "jni/jni_env_ext-inl.h"
#include "art_method-inl.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include <cstddef>
#include <cstring>
#include <initializer_list>

extern "C" int ArtDcevmSourceProbeNativeAccess(JNIEnv* env, jobject object,
    void* (*decode)(JNIEnv*, jobject)) {
  using namespace artdcevm::android13::native_access_abi;
  static_assert(art::Thread::ThreadFlagsOffset<art::PointerSize::k64>().Int32Value() == kThreadFlagsOffset);
  static_assert(static_cast<uint32_t>(art::ThreadState::kRunnable) == kRunnable);
  static_assert(static_cast<uint32_t>(art::ThreadState::kNative) == kNative);
  static_assert(sizeof(art::ObjPtr<art::mirror::Object>) == sizeof(void*));
  if (!decode || art::kIsDebugBuild || art::kPoisonHeapReferences) return 1;
  // These exact hooked entrypoints are registered FAST_NATIVE in the locked
  // java_lang_reflect_{Field,Constructor}.cc tables. Check the device's flags
  // too, without allocating JNI method IDs for all reflection accessors.
  for (const char* name : {"java/lang/reflect/Field", "java/lang/reflect/Constructor"}) {
    jclass type = env->FindClass(name);
    if (!type || env->ExceptionCheck()) return 9;
    bool matches = true;
    {
      art::ScopedObjectAccess soa(env);
      auto klass = soa.Decode<art::mirror::Class>(type);
      const bool field = name[18] == 'F';
      if (field) {
        for (const char* method_name : {"get", "getBoolean", "getByte", "getChar", "getDouble",
            "getFloat", "getInt", "getLong", "getShort", "set", "setBoolean", "setByte",
            "setChar", "setDouble", "setFloat", "setInt", "setLong", "setShort"}) {
          auto* method = klass->FindDeclaredDirectMethodByName(method_name, art::PointerSize::k64);
          if (!method) method = klass->FindDeclaredVirtualMethodByName(method_name, art::PointerSize::k64);
          matches &= method && method->IsNative() && method->IsFastNative() && !method->IsCriticalNative();
        }
      } else {
        auto* method = klass->FindDeclaredDirectMethodByName("newInstance0", art::PointerSize::k64);
        if (!method) method = klass->FindDeclaredVirtualMethodByName("newInstance0", art::PointerSize::k64);
        matches = method && method->IsNative() && method->IsFastNative() && !method->IsCriticalNative();
      }
    }
    env->DeleteLocalRef(type);
    if (!matches) return 10;
  }
  auto* self = art::ThreadForEnv(env);
  void* fixed_self;
  std::memcpy(&fixed_self, reinterpret_cast<unsigned char*>(env) + offsetof(EnvironmentPrefix, self), sizeof(fixed_self));
  if (fixed_self != self || self != art::Thread::Current()) return 2;
  uint32_t fixed_flags;
  auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(self) + kThreadFlagsOffset);
  fixed_flags = __atomic_load_n(flags, __ATOMIC_RELAXED);
  const auto old_state = self->GetState();
  if ((fixed_flags >> kStateShift) != static_cast<uint32_t>(old_state)) return 8;
  if (old_state != art::ThreadState::kNative && old_state != art::ThreadState::kRunnable) return 3;
  auto* jni = static_cast<art::JNIEnvExt*>(env);
  const auto cookie = jni->GetLocalRefCookie();
  auto segment_state = [&]() {
    art::ScopedObjectAccess soa(env);
    return jni->GetLocalsSegmentState().top_index;
  };
  const auto segment = segment_state();
  const bool exception_pending = self->IsExceptionPending();
  void* observed = decode(env, object);
  if (self->GetState() != old_state || jni->GetLocalRefCookie().top_index != cookie.top_index ||
      segment_state() != segment || self->IsExceptionPending() != exception_pending) return 4;
  {
    art::ScopedObjectAccess soa(env);
    if (decode(env, object) != soa.Decode<art::mirror::Object>(object).Ptr()) return 5;
    // The first result is not dereferenced across the potential suspension.
    if (!observed) return 6;
  }
  return self->GetState() == old_state ? 0 : 7;
}

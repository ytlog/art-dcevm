#include "platform/android13/runtime_access_abi.h"
#include "runtime.h"
#include "jit/jit.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include "scoped_thread_state_change-inl.h"
#include <list>
#include <memory>
#include <string>
#include <vector>
namespace art {
struct RuntimeLayoutProbe {
  static void Verify() {
    using Fixed = artdcevm::android13::runtime_access_abi::RuntimePrefix;
    static_assert(sizeof(std::string) == sizeof(artdcevm::android13::runtime_access_abi::PlatformContainer));
    static_assert(sizeof(std::vector<void*>) == 24 && alignof(std::vector<void*>) == 8);
    static_assert(sizeof(std::list<void*>) == 24 && alignof(std::list<void*>) == 8);
    static_assert(sizeof(std::unique_ptr<jni::JniIdManager>) == sizeof(void*));
    static_assert(offsetof(Runtime, callee_save_methods_) == offsetof(Fixed, callee_save_methods));
    static_assert(offsetof(Runtime, compiler_executable_) == offsetof(Fixed, compiler_executable));
    static_assert(offsetof(Runtime, default_stack_size_) == offsetof(Fixed, default_stack_size));
    static_assert(offsetof(Runtime, class_linker_) == offsetof(Fixed, class_linker));
    static_assert(offsetof(Runtime, jni_id_manager_) == offsetof(Fixed, jni_id_manager));
    static_assert(offsetof(Runtime, jit_) == offsetof(Fixed, jit));
    static_assert(offsetof(Runtime, jit_code_cache_) == offsetof(Fixed, jit_code_cache));
  }
};
}
extern "C" bool ArtDcevmSourceProbeRuntimeAccess(void* raw,
    const artdcevm::android13::runtime_access_abi::Services* services) {
  art::RuntimeLayoutProbe::Verify();
  auto* runtime = art::Runtime::Current();
  auto* jit = runtime->GetJit();
  return raw == runtime && services->class_linker == runtime->GetClassLinker() &&
      services->jni_id_manager == runtime->GetJniIdManager() && services->jit == jit &&
      services->jit_code_cache == (jit == nullptr ? nullptr : jit->GetCodeCache()) &&
      (jit == nullptr || jit->GetCodeCache() == runtime->GetJitCodeCache());
}

extern "C" int ArtDcevmSourceProbeJniIdDecode(JNIEnv* env, jobject calibration,
    void* (*method)(jmethodID), void* (*field)(jfieldID)) {
  jclass klass = env->GetObjectClass(calibration);
  if (!klass || env->ExceptionCheck()) return 1;
  auto mid = env->GetMethodID(klass, "compute", "()I");
  auto fid = env->GetFieldID(klass, "value", "I");
  if (!mid || !fid || env->ExceptionCheck()) { env->DeleteLocalRef(klass); return 2; }
  bool matches;
  {
    art::ScopedObjectAccess soa(env);
    auto* expected_method = art::jni::DecodeArtMethod(mid);
    auto* expected_field = art::jni::DecodeArtField(fid);
    matches = method(mid) == expected_method && field(fid) == expected_field &&
        method(reinterpret_cast<jmethodID>(expected_method)) == expected_method &&
        field(reinterpret_cast<jfieldID>(expected_field)) == expected_field &&
        method(nullptr) == nullptr && field(nullptr) == nullptr;
  }
  env->DeleteLocalRef(klass);
  return matches ? 0 : 3;
}

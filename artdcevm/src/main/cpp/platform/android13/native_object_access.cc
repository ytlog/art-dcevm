#include "platform/android13/native_object_access.h"
#include "platform/android13/native_object_access_abi.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
using Transition = void (*)(void*);
using DecodeObject = void* (*)(void*, jobject);
Transition enter_runnable, leave_runnable;
DecodeObject decode_object;
uint32_t State(void* self) {
  auto* flags = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(self) +
      native_access_abi::kThreadFlagsOffset);
  return __atomic_load_n(flags, __ATOMIC_RELAXED) >> native_access_abi::kStateShift;
}
void* DecodeForProbe(JNIEnv* env, jobject object) {
  NativeObjectAccess access(env);
  return access.Supported() ? access.Decode(object) : nullptr;
}
}

NativeObjectAccess::NativeObjectAccess(JNIEnv* env) : self_(nullptr), supported_(false),
    transitioned_(false) {
  std::memcpy(&self_, reinterpret_cast<unsigned char*>(env) +
      offsetof(native_access_abi::EnvironmentPrefix, self), sizeof(self_));
  if (!self_) return;
  const uint32_t state = State(self_);
  supported_ = state == native_access_abi::kRunnable || state == native_access_abi::kNative;
  if (supported_ && state == native_access_abi::kNative) {
    enter_runnable(self_);
    transitioned_ = true;
  }
}
NativeObjectAccess::~NativeObjectAccess() {
  if (transitioned_) leave_runnable(self_);
}
void* NativeObjectAccess::Decode(jobject object) const {
  if (!supported_) std::abort();
  return decode_object(self_, object);
}
bool ConfigureNativeObjectAccess(HookEngine& hooks, void* implementation, JNIEnv* env,
                                 jobject calibration, std::string* error) {
  using SelfOffset = int32_t (*)(size_t);
  using Probe = int (*)(JNIEnv*, jobject, void* (*)(JNIEnv*, jobject));
  auto end = reinterpret_cast<Transition>(hooks.FindSymbol("libart.so", "artJniMethodEnd"));
  auto start = reinterpret_cast<Transition>(hooks.FindSymbol("libart.so", "artJniMethodStart"));
  auto decode = reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so", native_access_abi::kDecodeJObject));
  auto self_offset = reinterpret_cast<SelfOffset>(hooks.FindSymbol("libart.so", native_access_abi::kSelfOffset));
  auto probe = reinterpret_cast<Probe>(dlsym(implementation, "ArtDcevmSourceProbeNativeAccess"));
  if (!end || !start || !decode || !self_offset || !probe) {
    *error = "Missing named ART native object access or source profile calibration";
    return false;
  }
  if (self_offset(sizeof(void*)) != offsetof(native_access_abi::EnvironmentPrefix, self)) {
    *error = "ART JNIEnv self offset differs from fixed release profile";
    return false;
  }
  enter_runnable = end; leave_runnable = start; decode_object = decode;
  const int result = probe(env, calibration, DecodeForProbe);
  if (result || env->ExceptionCheck()) {
    *error = "Native object access profile failed calibration at step " + std::to_string(result);
    return false;
  }
  return true;
}
}

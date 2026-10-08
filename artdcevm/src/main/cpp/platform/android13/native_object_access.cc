#include "platform/android13/native_object_access.h"
#include "platform/android13/native_object_access_abi.h"
#include <cstdlib>
#include <cstring>

namespace artdcevm::android13 {
namespace {
using Transition = void (*)(void*);
using DecodeObject = native_access_abi::DecodeObject;
Transition enter_runnable, leave_runnable;
DecodeObject decode_object;
uint32_t State(void* self) {
  auto* flags = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(self) +
      native_access_abi::kThreadFlagsOffset);
  return __atomic_load_n(flags, __ATOMIC_RELAXED) >> native_access_abi::kStateShift;
}

}

NativeObjectAccess::NativeObjectAccess(JNIEnv* env) : self_(nullptr), supported_(false),
    transitioned_(false) {
  if (!env || !decode_object || !enter_runnable || !leave_runnable) return;
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
void* DecodeRunnableObject(void* thread, jobject object) { return reinterpret_cast<void*>(decode_object(thread, object).reference); }
void* NativeObjectAccess::Decode(jobject object) const {
  if (!supported_) std::abort();
  return reinterpret_cast<void*>(decode_object(self_, object).reference);
}
bool ConfigureNativeObjectAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  using SelfOffset = native_access_abi::SelfOffset;
  if (!profile.Matched()) { *error = "Native object access requires a matched device profile"; return false; }
  auto end = reinterpret_cast<Transition>(hooks.FindSymbol("libart.so", "artJniMethodEnd"));
  auto start = reinterpret_cast<Transition>(hooks.FindSymbol("libart.so", "artJniMethodStart"));
  auto decode = reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so", native_access_abi::kDecodeJObject));
  auto self_offset = reinterpret_cast<SelfOffset>(hooks.FindSymbol("libart.so", native_access_abi::kSelfOffset));
  if (!end || !start || !decode || !self_offset) {
    *error = "Missing named ART native object access";
    return false;
  }
  if (self_offset(sizeof(void*)).value != offsetof(native_access_abi::EnvironmentPrefix, self)) {
    *error = "ART JNIEnv self offset differs from fixed release profile";
    return false;
  }
  enter_runnable = end; leave_runnable = start; decode_object = decode;
  return true;
}
}

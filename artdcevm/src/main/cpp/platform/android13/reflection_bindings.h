#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
#include <unordered_map>

namespace artdcevm::android13 {
// Prefer ELF names. Only missing reflection native entries may use a binding
// identified by Java name/signature after owned ArtMethod calibration.
class ReflectionBindings final : public HookEngine {
 public:
  explicit ReflectionBindings(HookEngine& delegate, bool force_native = false)
      : delegate_(delegate), force_native_(force_native) {}
  bool Configure(JNIEnv*, const DeviceProfile&, std::string*);
  bool Initialize(std::string* error) override { return delegate_.Initialize(error); }
  void* FindSymbol(const char*, const char*) override;
  void* Install(void* target, void* proxy, void** original, std::string* error) override {
    return delegate_.Install(target, proxy, original, error);
  }
  bool Remove(void* token, std::string* error) override { return delegate_.Remove(token, error); }
 private:
  HookEngine& delegate_;
  bool force_native_;
  std::unordered_map<std::string, void*> bindings_;
};
}

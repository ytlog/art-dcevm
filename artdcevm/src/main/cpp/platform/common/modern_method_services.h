#pragma once
#include "api/hook_engine.h"
#include "platform/common/method_array_view.h"
#include "platform/common/native_method_binding_calibration.h"
#include "platform/common/structural_methods.h"
#include <memory>
namespace artdcevm::modern_methods {
// Actual source commit adapter. Prepare before mutation, within owned Runnable.
// Both table Views must already have complete source/DEX/reflection calibration;
// the classes and arrays remain rooted across preparation and the commit.
// Mutating methods require the original ART exclusive-mutator/GC boundary.
class Service final : public structural_methods::Services {
 public:
  using Retired = bool (*)(void*);
  static std::unique_ptr<Service> Prepare(HookEngine&,
      const runnable_jni::Context&,
      abi::arm64::android_common::FromReflectedMethod,
      const method_identity::Oracle& registered_native, const void* binding,
      void* observed_class_linker, void* old_class, const method_array::View&,
      void* new_class, const method_array::View&, Retired, std::string* error);
  size_t Count(void*) const override;
  void* At(void*,size_t) const override;
  uint32_t Flags(void*) const override;
  void* DeclaringClass(void*) const override;
  bool SameIdentity(void*,void*) const override;
  bool IsRetired(void*) const override;
  void TransferNativeBinding(void*,void*) const override;
  void AddFlags(void*,uint32_t) const override;
  void SetObsoleteEntry(void*) const override;
 private:
  Service() = default;
  const method_array::View& Table(void*) const;
  void RequireMember(void*) const;
  using Root = void* (*)(void*);
  using Same = bool (*)(void*,void*);
  using Obsolete = void (*)(void*,void*);
  Root root_ = nullptr;
  Same same_ = nullptr;
  Obsolete obsolete_ = nullptr;
  Retired retired_ = nullptr;
  void* class_linker_ = nullptr;
  void* old_class_ = nullptr;
  void* new_class_ = nullptr;
  method_array::View old_, new_;
};
}  // namespace artdcevm::modern_methods

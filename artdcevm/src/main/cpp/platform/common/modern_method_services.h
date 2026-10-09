#pragma once
#include "api/hook_engine.h"
#include "platform/common/method_array_view.h"
#include "platform/common/native_method_binding_calibration.h"
#include "platform/common/structural_methods.h"
#include <memory>
#include "platform/common/prepared_root_bindings.h"
namespace artdcevm::modern_methods {
class Service;
class Binding final {
 public:
  ~Binding()=default;
  Binding(const Binding&)=delete;
 private:
  friend class Service;
  Binding()=default;
  void* (*root_)(void*)=nullptr;
  bool (*same_)(void*,void*)=nullptr;
  void (*obsolete_)(void*,void*)=nullptr;
  void* class_linker_=nullptr;
  const method_metadata::Layout* layout_=nullptr;
};
// Source commit adapter. Bind JNI oracles during bootstrap; prepare tables
// inside the transaction owner's rooted native-only final boundary.
// Both table Views must already have complete source/DEX/reflection calibration;
// the classes and arrays remain rooted across preparation and the commit.
// Mutating methods require the original ART exclusive-mutator/GC boundary.
class Service final : public structural_methods::Services {
 public:
  using Retired = bool (*)(void*);
  // JNI/native registration oracle belongs to bootstrap, never final commit.
  static std::unique_ptr<Binding> BindOwned(HookEngine&,
      const runnable_jni::Context&,
      abi::arm64::android_common::FromReflectedMethod,
      const method_identity::Oracle&,const void* registered_binding,
      void* observed_class_linker,std::string* error);
  static std::unique_ptr<Binding> BindOwned(HookEngine&,
      const runnable_jni::Context&,
      abi::arm64::android_common::FromReflectedMethod,
      const method_identity::Oracle&,const void* registered_binding,
      void* observed_class_linker,method_identity::Model,std::string* error);
  // Native-only preparation: no JNI, managed allocation or suspension. The
  // original ART transaction already roots both tables and disables GC.
  static std::unique_ptr<Service> PrepareTables(const Binding&,
      void* old_class,const method_array::View&,
      void* new_class,const method_array::View&,Retired,std::string* error);
  static std::unique_ptr<Service> Prepare(HookEngine&,
      const runnable_jni::Context&,
      abi::arm64::android_common::FromReflectedMethod,
      const method_identity::Oracle& registered_native, const void* binding,
      void* observed_class_linker, void* old_class, const method_array::View&,
      void* new_class, const method_array::View&, Retired, std::string* error);
  // Fixed modern ART heap replacement rewrites native declaring-class roots.
  // Restore only the old non-copied methods before the class is retained.
  void RestoreOldDeclaringClasses()const;
  bool MatchesClassTable(void*)const noexcept;
  bool CollectManagedRoots(const prepared_roots::Registrar& r){if(!roots_.Add(&old_class_,r)||!roots_.Add(&new_class_,r))return false;roots_.Seal();return true;}
  bool CanRefreshManagedRoots(const prepared_roots::Resolver& r)const noexcept{return roots_.CanRefresh(r);}
  void RefreshManagedRoots(const prepared_roots::Resolver& r)noexcept{roots_.Refresh(r);}
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
  prepared_roots::Bindings roots_;
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
  const method_metadata::Layout* layout_ = nullptr;
};
}  // namespace artdcevm::modern_methods

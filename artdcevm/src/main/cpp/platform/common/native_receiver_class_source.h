#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/common/class_relationships.h"
#include "platform/common/native_receiver_relations.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/runnable_jni_bridge.h"
#include <vector>
namespace artdcevm::receiver_guard {
// Actual original Class history/resolved-direct-interface leaves, with the
// independently calibrated Class/field/IfTable declaration. Binding alone is
// no guard/publication proof. The caller owns the current source mutator scope.
class ClassSource final {
 public:
 bool Bind(HookEngine&,const modern_field_metadata::FieldMetadata&,void*(*actual_current)(),std::string*,void*actual_class_linker=nullptr);
 // Ordinary calibration access uses OS mapping bounds; it must not be bound
 // as the permanent Guard no-allocation/final access authority.
 ClassAccess OrdinaryAccess()noexcept;
 // Non-owning, thread-affine access for one caller-owned GC-stable read window.
 // The context's actual owner must reject expired windows. Never retain this
 // stack owner/context across a safepoint or bind it as permanent dispatch proof.
 class ReadWindow final {
  public:
  ReadWindow(const ClassSource&,const modern_field_metadata::ReadContext&)noexcept;
  ReadWindow(const ReadWindow&)=delete;ReadWindow&operator=(const ReadWindow&)=delete;
  ClassAccess Access()noexcept;
  private:
  bool Current()const noexcept;
  static bool Read(void*,void*,ClassView*)noexcept;
  static bool InterfaceAt(void*,void*,size_t,void**)noexcept;
  static bool DirectAt(void*,void*,size_t,void**)noexcept;
  static bool Obsolete(void*,void*,void**)noexcept;
  const ClassSource&source_;const modern_field_metadata::ReadContext&context_;
  void*owner_;decltype(modern_field_metadata::ReadContext::extent)extent_;void*self_;
 };
 bool CalibrateInterfaces(const runnable_jni::Context&,jobject strong_fixture,std::string*);
 bool InterfacesCalibrated()const noexcept{return interfaces_calibrated_;}
 // Public direct edges/parent/loader were collected before the native borrow.
 // This checks only that current original Class episode, not old-history or
 // permanent dispatch readiness. expected_owner is its real native member root.
 bool Observe(const runnable_jni::Context&,jobject klass,jobject parent,jobject loader,const std::vector<jobject>&direct,bool public_interface,void*expected_owner)noexcept;
 private:
 static bool Read(void*,void*,ClassView*)noexcept;
 static bool InterfaceAt(void*,void*,size_t,void**)noexcept;
 static bool DirectAt(void*,void*,size_t,void**)noexcept;
 static bool Obsolete(void*,void*,void**)noexcept;
 bool ReadIn(void*,ClassView*,const modern_field_metadata::ReadContext*)const noexcept;
 bool InterfaceIn(void*,size_t,void**,const modern_field_metadata::ReadContext*)const noexcept;
 bool DirectIn(void*,size_t,void**,const modern_field_metadata::ReadContext*)const noexcept;
 bool ObsoleteIn(void*,void**,const modern_field_metadata::ReadContext*)const noexcept;
 bool Definition(void*,evolution::MappedDexMetadata*,evolution::MappedDexClassIdentity*,const modern_field_metadata::ReadContext*)const;
 bool ProxyInterfaces(void*,void**,size_t*,const modern_field_metadata::ReadContext* =nullptr)const;
 bool Current()const noexcept{return bound_&&current_&&current_();}
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;void*(*current_)()=nullptr;
 abi::arm64::android_common::ObsoleteClass obsolete_=nullptr;abi::arm64::android_common::DirectInterface direct_=nullptr;abi::arm64::android_common::DirectInterfaceType direct_type_=nullptr;
 abi::arm64::android_common::LookupDirectInterface lookup_direct_=nullptr;void*linker_=nullptr;bool bound_=false,interfaces_calibrated_=false;
};
}

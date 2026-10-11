#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/bounded_managed_roots.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/modern_field_metadata.h"
#include <vector>
#include <cstring>
namespace artdcevm::native_selective_interpretation {
// Ordinary producer supplies independent public/Dex identity and the retained
// declaring and containing Class roots (distinct for copied/default methods).
// Activation independently checks native array membership.
struct Method {void* method=nullptr;bounded_managed_roots::Slot declaring_root{},containing_root{};uint32_t dex_index=UINT32_MAX,modifiers=0;};
namespace detail {
// Bounded source predicates only: neither function grants mutator access,
// lifetime, graph completeness or an Instrumentation owner.
inline bool Identity(const method_metadata::Layout&layout,const void*method,size_t extent,
 uint32_t index,uint32_t modifiers)noexcept{
 method_metadata::Identity identity{};
 return method_metadata::ReadIdentity(layout,method,extent,&identity)&&index!=UINT32_MAX&&
  identity.dex_method_index==index&&!(identity.access_flags&0x40500u)&&
  (identity.access_flags&0xffffu)==(modifiers&0xffffu);
}
inline bool Member(const void*array,size_t extent,const void*method,size_t stride)noexcept{
 const auto base=reinterpret_cast<uintptr_t>(array),address=reinterpret_cast<uintptr_t>(method);
 if(!base||base%8||extent<8||!stride||base>UINTPTR_MAX-8)return false;
 uint32_t count=0;std::memcpy(&count,array,4);
 if(count>(extent-8)/stride||count>(UINTPTR_MAX-base-8)/stride)return false;
 const auto begin=base+8;return address>=begin&&address<begin+size_t(count)*stride&&!( (address-begin)%stride );
}
}
struct Report {size_t selected=0,already_interpreted=0,activated=0;bool requires_stack_drain=false;const char*failure=nullptr;};
struct RefundReport {
 size_t selected=0,removed=0,already_absent=0,foreign_retained=0,pending=0;
 bool complete=false;const char*failure=nullptr;
};
class Service final {
 public:
  Service()=default;Service(const Service&)=delete;Service&operator=(const Service&)=delete;
  // Ordinary JNI only; calls the existing original receiver calibration.
  // No raw receiver/witness or caller-supplied Boolean can sign this owner.
  bool CalibrateOwner(HookEngine&,JNIEnv*,std::string*);
  bool Prepare(HookEngine&,
      const method_identity::Calibration&,const modern_field_metadata::FieldMetadata&,
      const bounded_managed_roots::Store&,const Method*,size_t,std::string*);
  // Only the actual calibrated SSA/exclusive mutator Session can activate.
  // This retains ART's deoptimized set; it neither Undeoptimizes nor claims an
  // already-running compiled/inline frame has become interpreted. Resume and
  // independently rescan all selected physical/inline carriers before mutation.
  bool Activate(const native_source_stop::Session&,void*self,
      const modern_field_metadata::ReadContext&,Report*) noexcept;
  bool ValidateRetained(const native_source_stop::Session&,void*self,
      const modern_field_metadata::ReadContext&,Report*)const noexcept;
  bool ValidateIdentities(const native_source_stop::Session&,void*self,
      const modern_field_metadata::ReadContext&,const char**failure)const noexcept;
  // Bind the fixed named Undeoptimize declaration before activation. Ordinary
  // preparation only; it borrows the independently calibrated original owner.
  bool PrepareRefund(HookEngine&,std::string*);
  // Removes only entries this Service actually attempted to add, including a
  // partially failed activation. Preexisting/foreign entries are preserved.
  // Caller must first prove no selected frame can retain discarded metadata.
  // Completion is owned-set cleanup only, never Class/cache/GC reclamation.
  bool Refund(const native_source_stop::Session&,void*self,
      const modern_field_metadata::ReadContext&,RefundReport*)noexcept;
  size_t PendingRefund()const noexcept;
  method_metadata::Layout MethodLayout()const noexcept{return layout_;}
  size_t SelectedCount()const noexcept{return methods_.size();}
  const std::vector<Method>& SelectedMethods()const noexcept{return methods_;}
  bool Contains(void* method)const noexcept;
 private:
  using Deopt=void(*)(void*,void*);using Query=bool(*)(void*,void*);
  enum class Phase {Empty,Prepared,Activated,Refunded};
  enum class Entry {Untouched,Preexisting,Owned,Released};
  bool Window(const native_source_stop::Session&,void*)const noexcept;
  bool Check(const Method&,const modern_field_metadata::ReadContext&,const char**)const noexcept;
  void*runtime_=nullptr;void*instrumentation_=nullptr;void**runtime_slot_=nullptr;
  const modern_field_metadata::FieldMetadata*metadata_=nullptr;
  const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0;
  method_metadata::Layout layout_{};std::vector<Method>methods_;
  std::vector<Entry>entries_;
  Deopt deopt_=nullptr,undeopt_=nullptr;Query query_=nullptr;Phase phase_=Phase::Empty;
};
}

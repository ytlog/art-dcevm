#pragma once
#include "platform/common/native_boot_aot_unaffected.h"
#include "platform/common/native_boot_aot_prefix.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_boot_return_continuation.h"
namespace artdcevm::native_boot_aot_unaffected {
class Source;
// A structural suffix receipt is bound to one concrete stopped Source. It is
// neither an entered-callee/dispatch permission nor a runtime return owner.
class SuffixReceipt final {
 public:
 SuffixReceipt()=default;
 SuffixReceipt(const SuffixReceipt&)=delete;SuffixReceipt&operator=(const SuffixReceipt&)=delete;
 SuffixReceipt(SuffixReceipt&&)=delete;SuffixReceipt&operator=(SuffixReceipt&&)=delete;
 void* Method()const noexcept{return method_;}
 uint32_t DexPc()const noexcept{return pc_;}
 const receiver_guard::CodeView& Code()const noexcept{return code_;}
 const native_boot_return_continuation::Shape& Shape()const noexcept{return shape_;}
 private:
 friend class Source;
 void Clear()noexcept{owner_=nullptr;plan_=nullptr;stop_=nullptr;catalog_=nullptr;roots_=nullptr;self_=nullptr;method_=nullptr;pc_=0;entry_=nullptr;code_={};shape_={};}
 const Source*owner_=nullptr;const source_commit::Plan*plan_=nullptr;
 const native_source_stop::Session*stop_=nullptr;const native_boot_method_catalog::Rooted*catalog_=nullptr;
 const source_handle_roots::StoppedReader*roots_=nullptr;void*self_=nullptr;void*method_=nullptr;uint32_t pc_=0;
 const native_boot_method_catalog::Method*entry_=nullptr;
 receiver_guard::CodeView code_{};native_boot_return_continuation::Shape shape_{};
};
// Borrowed for exactly one synchronous source Run. Every final Verify uses its
// same actual stop, fresh Class slots, immutable public body and source CodeItem.
class Source final:public Proof {
 public:
 Source(const source_commit::Plan&plan,const native_source_stop::Session&stop,void*self,
        const native_boot_method_catalog::Rooted&catalog,const source_handle_roots::StoppedReader&roots,
        const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&methods,
        const native_abstract_instances::Service&vtables,const receiver_guard::SourceProducer&code)
  :plan_(plan),stop_(stop),self_(self),catalog_(catalog),roots_(roots),metadata_(metadata),methods_(methods),vtables_(vtables),code_(code){}
 bool Verify(void*,uint32_t,const code_info_view::View&,uint32_t,
             const decoded_frame_recovery::RegisterRange&,const modern_field_metadata::ReadContext&,const char**failure=nullptr)const noexcept override;
 // Structural source observation, not a replacement for Verify. The stopped
 // caller must separately own callee dispatch and complete frame-root proofs.
 bool InspectContinuation(void*,uint32_t,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  receiver_guard::CodeView*,native_boot_return_continuation::Shape*,const char**failure=nullptr)const noexcept override;
 bool InspectExceptionalEntry(void*,uint32_t,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  receiver_guard::CodeView*,native_boot_return_continuation::Shape*,const char**failure=nullptr)const noexcept override;
 bool CurrentCalleeCode(void*method,const modern_field_metadata::ReadContext&read,receiver_guard::CodeView*out)const noexcept override;
 bool ObserveSuffix(void*,uint32_t,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  SuffixReceipt*,const char**failure=nullptr)const noexcept;
 bool ValidateSuffix(const SuffixReceipt&,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  const char**failure=nullptr)const noexcept;
 private:
 bool OwnsWindow()const noexcept;
 bool Prefix(void*,const native_boot_method_catalog::Method&,const modern_field_metadata::ReadContext&,
             method_metadata::Identity*,const char**,bool require_static=true)const noexcept;
 bool InspectEntry(void*,uint32_t,native_boot_return_continuation::Entry,
  const modern_field_metadata::ReadContext&,native_boot_return_continuation::HandlerStorage,
  native_boot_return_continuation::Scratch,receiver_guard::CodeView*,native_boot_return_continuation::Shape*,const char**)const noexcept;
 const source_commit::Plan&plan_;const native_source_stop::Session&stop_;void*self_;
 const native_boot_method_catalog::Rooted&catalog_;const source_handle_roots::StoppedReader&roots_;
 const modern_field_metadata::FieldMetadata&metadata_;const method_identity::Calibration&methods_;
 const native_abstract_instances::Service&vtables_;const receiver_guard::SourceProducer&code_;
};
}

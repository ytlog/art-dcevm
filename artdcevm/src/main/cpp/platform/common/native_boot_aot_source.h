#pragma once
#include "platform/common/native_boot_aot_unaffected.h"
#include "platform/common/native_boot_aot_prefix.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_source_stop.h"
namespace artdcevm::native_boot_aot_unaffected {
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
 private:
 const source_commit::Plan&plan_;const native_source_stop::Session&stop_;void*self_;
 const native_boot_method_catalog::Rooted&catalog_;const source_handle_roots::StoppedReader&roots_;
 const modern_field_metadata::FieldMetadata&metadata_;const method_identity::Calibration&methods_;
 const native_abstract_instances::Service&vtables_;const receiver_guard::SourceProducer&code_;
};
}

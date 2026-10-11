#pragma once
#include "platform/common/native_boot_method_catalog.h"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_source_stop.h"

namespace artdcevm::native_interface_dispatch_domain {
class Source;
// Only dispatch dependencies are covered. This receipt does not prove that an
// invocation entered its target, or own a native helper's remaining execution.
// Validation always re-reads current roots; no movable Class is retained here.
class Certificate final {
 public:
 Certificate(const Certificate&)=delete;Certificate&operator=(const Certificate&)=delete;
 Certificate(Certificate&&)=delete;Certificate&operator=(Certificate&&)=delete;
 void* Caller()const noexcept{return caller_;}
 uint32_t DexPc()const noexcept{return pc_;}
 void* Declaration()const noexcept{return declaration_;}
 private:
 friend class Source;
 Certificate()=default;
 void Clear()noexcept{source_=nullptr;caller_=declaration_=nullptr;pc_=0;}
 const Source*source_=nullptr;void*caller_=nullptr;void*declaration_=nullptr;uint32_t pc_=0;
};
class Source final {
 public:
 Source(const native_hierarchy_graph::Authority&hierarchy,
        const native_schema_guard_verifier::Verifier&callers,
        const native_source_class_loading::Run&loading,const void*driver,
        const native_source_stop::Session&stop,void*self,const source_commit::Plan&plan,
        const native_boot_method_catalog::Catalog&catalog,
        const native_boot_method_catalog::Rooted&rooted,
        const source_handle_roots::StoppedReader&roots,
        const modern_field_metadata::FieldMetadata&metadata,
        const method_metadata::Layout&methods,const receiver_guard::StoppedCodeReader&code)
  :hierarchy_(hierarchy),callers_(callers),loading_(loading),driver_(driver),stop_(stop),self_(self),plan_(plan),
   catalog_(catalog),rooted_(rooted),roots_(roots),metadata_(metadata),methods_(methods),code_(code){}
 Source(const Source&)=delete;Source&operator=(const Source&)=delete;
 // Ordinary preparation only; Observe/Validate never allocate.
 static std::unique_ptr<Certificate> PrepareCertificate(){return std::unique_ptr<Certificate>(new Certificate);}
 bool Observe(void*caller,uint32_t pc,const modern_field_metadata::ReadContext&,
              Certificate*,const char**failure=nullptr)const noexcept;
 bool Validate(const Certificate&,const modern_field_metadata::ReadContext&,
               const char**failure=nullptr)const noexcept;
 private:
 bool Verify(void*,uint32_t,const modern_field_metadata::ReadContext&,void**declaration,const char**)const noexcept;
 const native_hierarchy_graph::Authority&hierarchy_;const native_schema_guard_verifier::Verifier&callers_;
 const native_source_class_loading::Run&loading_;const void*driver_;const native_source_stop::Session&stop_;
 void*self_;const source_commit::Plan&plan_;const native_boot_method_catalog::Catalog&catalog_;
 const native_boot_method_catalog::Rooted&rooted_;const source_handle_roots::StoppedReader&roots_;
 const modern_field_metadata::FieldMetadata&metadata_;method_metadata::Layout methods_;
 const receiver_guard::StoppedCodeReader&code_;
};
}

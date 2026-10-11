#pragma once
#include "platform/common/art_method_metadata.h"
#include "platform/common/native_boot_method_catalog.h"
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/modern_field_metadata.h"
namespace artdcevm::native_interface_dispatch {
struct Match {void* interface=nullptr;void* declaration=nullptr;void* target=nullptr;uint16_t slot=0;};
// Fixed ARM64 PointerArray is mirror::LongArray: Object8, length4, padding4,
// uint64 payload. A pure bounded reader; its result is not dispatch authority.
bool ReadPointerSlot(const void*array,size_t extent,uint32_t slot,void**out)noexcept;
// Shared bounded native DEX namespace reader. This is a read result, not a
// declaration-resolution or dispatch permission.
bool ReadCurrentNamespace(const modern_field_metadata::FieldMetadata&,void*,
 const modern_field_metadata::ReadContext&,evolution::MappedDexMetadata*,field_copy::CopyClassView*)noexcept;
// No resolving, JNI, allocation or OS mapping queries. All movable references
// are borrowed from the same actual stopped walker/root window. Caller still
// owns the adjacent-frame entry proof and complete continuation dependencies.
// caller CodeView must come from that exact current caller via calibrated
// SourceProducer::Code, and receiver from the actual younger callee map/root
// binding. A readable DEX or object pointer alone is never such provenance.
class Source;
// Only the concrete current-root/source-Dex/IfTable verifier can construct this
// typed capability. Scanner consumers dispatch through it without linking a
// concrete implementation into fixtures that never install this capability.
class Proof {
 public:
 virtual ~Proof()=default;
 virtual bool Verify(const receiver_guard::CodeView&,uint32_t,void*,void*,
                     const modern_field_metadata::ReadContext&,Match*,
                     const char**failure=nullptr)const noexcept=0;
 private:
 friend class Source;
 Proof()=default;
 Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
};
class Source final:public Proof {
 public:
 Source(const modern_field_metadata::FieldMetadata&m,const method_metadata::Layout&layout,
        const native_boot_method_catalog::Rooted&catalog,const source_handle_roots::StoppedReader&roots)
  :metadata_(m),layout_(layout),catalog_(catalog),roots_(roots){}
 bool Verify(const receiver_guard::CodeView&caller,uint32_t pc,void*receiver,
             void*actual_callee,const modern_field_metadata::ReadContext&,Match*,
             const char**failure=nullptr)const noexcept override;
 private:
 const modern_field_metadata::FieldMetadata&metadata_;method_metadata::Layout layout_;
 const native_boot_method_catalog::Rooted&catalog_;const source_handle_roots::StoppedReader&roots_;
};
}

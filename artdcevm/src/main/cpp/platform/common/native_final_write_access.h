#pragma once
#include "platform/common/native_final_write_guard.h"
#include "platform/common/native_final_field_aliases.h"
#include "platform/common/native_reference_decoder.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <memory>
namespace artdcevm::final_write_guard {
class AliasBatch;
// Process-owned native access for the sole field dispatcher. Binding supplies
// identity readers, never an enforcement proof. Metadata/decoder outlive this owner.
class NativeAccess final {
 public:
 static std::unique_ptr<NativeAccess> Create(HookEngine&,
     const modern_field_metadata::FieldMetadata&,const method_identity::Calibration&,
     native_reference_decoder::Decoder&,std::string*);
 NativeAccess(const NativeAccess&)=delete;NativeAccess&operator=(const NativeAccess&)=delete;
 Guard& Writer()noexcept{return writer_;}
 // Commit publishes only preallocated aliases whose weak handles are already
 // registered in the shared decoder. Cleanup deactivates before Forget/delete.
 final_field_aliases::Set& Aliases()noexcept{return aliases_;}
 private:
 friend class AliasBatch;
 using Throw=void(*)(void*,const char*,const char*);
 NativeAccess(const modern_field_metadata::FieldMetadata&,native_reference_decoder::Decoder&,
              abi::arm64::android11::CurrentThread,Throw);
 static bool Field(void*,void*,uint32_t*,void**);
 static bool Method(void*,void*,void**);
 static bool Flags(void*,void*,uint32_t*);
 static void Raise(void*,void*,const char*,const char*);
 static bool Equivalent(void*,void*,void*,void*);
 static bool Weak(void*,void*,void*,void**);
 static void* FieldOwner(void*,void*);
 static void* MethodOwner(void*,void*);
 const modern_field_metadata::FieldMetadata&metadata_;
 native_reference_decoder::Decoder&references_;
 abi::arm64::android11::CurrentThread current_;Throw throw_;
 final_field_aliases::Set aliases_;Guard writer_;
 std::mutex alias_cleanup_;
};
}

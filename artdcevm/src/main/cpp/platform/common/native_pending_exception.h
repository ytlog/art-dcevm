#pragma once
#include "platform/common/source_handle_roots.h"
namespace artdcevm::native_pending_exception {
// Current-thread scalar null test only: never decode or retain the Throwable.
// The relative slot comes from the fixed TLS declaration, with its prefix
// independently established by field/card calibration. Calibrate checks actual
// public no-exception/Throw/clear behavior in the owning ordinary Native scope.
class Service final {
 public:
 bool Calibrate(HookEngine&,JNIEnv*,JavaVM*,const modern_field_storage::Service&,
                const source_handle_roots::Service&,std::string*);
 bool Read(void*actual_thread,bool*)const noexcept;
 bool Calibrated()const noexcept{return ready_;}
 private:
 bool ReadCandidate(void*,bool*)const noexcept;
 abi::arm64::android11::CurrentThread current_=nullptr;
 size_t offset_=0;bool ready_=false;
};
}

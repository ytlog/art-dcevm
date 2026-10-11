#pragma once
#include "platform/common/source_handle_roots.h"
#include <cstring>
namespace artdcevm::native_pending_exception {
// Current-thread scalar null test only: never decode or retain the Throwable.
// The relative slot comes from the fixed TLS declaration, with its prefix
// independently established by field/card calibration. Calibrate checks actual
// public no-exception/Throw/clear behavior in the owning ordinary Native scope.
class Service final {
 public:
 bool Calibrate(HookEngine&,JNIEnv*,JavaVM*,const modern_field_storage::Service&,
                const source_handle_roots::Service&,std::string*);
 bool Read(void*actual_thread,bool*out)const noexcept{return ready_&&ReadCandidate(actual_thread,out);}
 bool Calibrated()const noexcept{return ready_;}
 private:
 bool ReadCandidate(void*self,bool*out)const noexcept{
  if(!out||!self||!current_||current_()!=self||!offset_||
     reinterpret_cast<uintptr_t>(self)%alignof(void*)||
     offset_>UINTPTR_MAX-sizeof(void*)||reinterpret_cast<uintptr_t>(self)>UINTPTR_MAX-offset_-sizeof(void*))return false;
  uintptr_t exception=0;
  std::memcpy(&exception,static_cast<const unsigned char*>(self)+offset_,sizeof(exception));
  if(current_()!=self)return false;*out=exception!=0;return true;
 }
 abi::arm64::android11::CurrentThread current_=nullptr;
 size_t offset_=0;bool ready_=false;
};
}

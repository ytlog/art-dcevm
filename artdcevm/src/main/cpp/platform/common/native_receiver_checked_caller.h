#pragma once
#include "platform/common/art_method_metadata.h"
#include "platform/abi/arm64/android17/checked_method_resolution.h"
namespace artdcevm::receiver_guard {
// Native-only source access under the actual listener's current Runnable
// scope. Root is the independently calibrated ArtMethod declaring-Class leaf;
// clear is the original named source implementation, never a raw flag write.
struct CheckedCallerAccess {
 void*owner=nullptr;
 bool(*current)(void*,void*)noexcept=nullptr;
 bool(*extent)(void*,void*,const void*,size_t*)noexcept=nullptr;
 void*(*declaring)(void*,void*,void*)noexcept=nullptr;
 abi::arm64::android17::ClearCallerAccessChecks clear=nullptr;
};
inline bool EnsureCheckedCaller(const CheckedCallerAccess&a,void*self,void*method)noexcept {
 if(!self||!method||!a.current||!a.extent||!a.declaring||!a.clear||!a.current(a.owner,self))return false;
 size_t extent=0;method_metadata::Identity before;
 if(!a.extent(a.owner,self,method,&extent)||!method_metadata::ReadIdentity(method_metadata::kAndroid12Onward,method,extent,&before)||before.dex_method_index==UINT32_MAX||(before.access_flags&0x500u)||!a.current(a.owner,self))return false;
 void*klass=a.declaring(a.owner,self,method);if(!klass||!a.current(a.owner,self))return false;
 if(before.access_flags&abi::arm64::android17::kSkipAccessChecks){
  a.clear(klass,abi::arm64::android17::MethodPointerSize::k64);
 }
 // Exact root/thread/identity and actual flags are reread even when the caller
 // was already checked. A retained observation cannot authorize a later call.
 if(!a.current(a.owner,self)||a.declaring(a.owner,self,method)!=klass||!a.extent(a.owner,self,method,&extent))return false;
 method_metadata::Identity after;
 return method_metadata::ReadIdentity(method_metadata::kAndroid12Onward,method,extent,&after)&&
        after.dex_method_index==before.dex_method_index&&
        after.access_flags==(before.access_flags&~abi::arm64::android17::kSkipAccessChecks)&&a.current(a.owner,self);
}
}

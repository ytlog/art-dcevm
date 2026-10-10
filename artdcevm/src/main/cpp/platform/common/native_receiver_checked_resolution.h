#pragma once
#include "platform/common/native_receiver_resolution_source.h"
#include "platform/common/native_receiver_bytecode.h"
namespace artdcevm::receiver_guard {
// The exact non-Thread ClassLinker declaration checks access regardless of
// ArtMethod::SkipAccessChecks. Only its actual pending exception authorizes
// OriginalException; null alone never does. Current Thread is rechecked after
// every callback, because resolution may suspend.
struct CurrentCheckedAccess {
 void* owner=nullptr;
 bool(*current)(void*,void*)noexcept=nullptr;
 bool(*pending)(void*,void*,bool*)noexcept=nullptr;
 CheckedResolveMethodCurrent original=nullptr;
};
struct CurrentCheckedResult {void* method=nullptr;bool original_exception=false;};
inline CurrentCheckedResult ResolveCurrentChecked(const CurrentCheckedAccess&a,
    void*self,void*linker,uint32_t index,void*caller,InvokeKind kind)noexcept {
 if(!self||!linker||!caller||!a.current||!a.pending||!a.original||
    kind>InvokeKind::Interface||!a.current(a.owner,self))return {};
 bool pending=false;
 if(!a.pending(a.owner,self,&pending)||!a.current(a.owner,self))return {};
 if(pending)return {nullptr,true};
 constexpr uint32_t invoke_type[]={2,3,1,0,4};
 void* method=a.original(linker,index,caller,invoke_type[unsigned(kind)]);
 if(!a.current(a.owner,self)||!a.pending(a.owner,self,&pending)||!a.current(a.owner,self))return {};
 if(pending)return {nullptr,true};
 return {method,false};
}
}

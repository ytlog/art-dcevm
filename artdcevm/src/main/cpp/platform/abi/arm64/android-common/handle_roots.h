#pragma once
#include "platform/abi/arm64/android16/thread.h"
namespace artdcevm::abi::arm64::android_common {
// Pinned 11--17 thread.h pointer-sized TLS prefix through top_handle_scope.
// The absolute prefix start is never selected by API: field-storage's actual
// independent card oracle establishes its location, then source handles and
// the named root visitor establish these additional declarations.
struct alignas(8) HandleThreadPrefix {
 uint8_t* card_table;void* exception;uint8_t* stack_end;
 android16::ManagedStack managed_stack;
 uintptr_t suspend_trigger;void* jni_env;void* tmp_jni_env;void* self;
 void* opeer;void* jpeer;uint8_t* stack_begin;size_t stack_size;
 void* deps_or_stack_trace;void* wait_next;void* monitor_enter_object;void* top_handle_scope;
};
#pragma pack(push,4)
struct HandleScopeHeader {void* link;int32_t count;};
#pragma pack(pop)
struct NativeRootInfo {const void* vptr;uint32_t type,thread_id;};
struct HandleRootVisitor {
 virtual ~HandleRootVisitor()=default;
 virtual void VisitRoots(void***,size_t,const NativeRootInfo&)=0;
 virtual void VisitRoots(uint32_t**,size_t,const NativeRootInfo&)=0;
};
using HandleVisitRoots=void (*)(void*,HandleRootVisitor*,uint32_t);
inline constexpr char kHandleVisitRoots[]="_ZN3art6Thread21HandleScopeVisitRootsEPNS_11RootVisitorEj";
inline constexpr size_t kHandleReferences=sizeof(HandleScopeHeader);
static_assert(sizeof(HandleScopeHeader)==12&&alignof(HandleScopeHeader)==4);
static_assert(offsetof(HandleThreadPrefix,top_handle_scope)==136&&offsetof(HandleThreadPrefix,self)==72&&offsetof(HandleThreadPrefix,jni_env)==56);
static_assert(sizeof(NativeRootInfo)==16&&offsetof(NativeRootInfo,type)==8&&sizeof(HandleRootVisitor)==8);
}

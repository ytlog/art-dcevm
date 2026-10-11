#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/android17/checked_method_resolution.h"
#include <jni.h>
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/native_receiver_resolution_source.h"
#include "platform/common/code_item_metadata.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/common/method_identity_calibration.h"
namespace artdcevm::receiver_guard {
// Native producer for the actual listener thread. Geometry and method identity
// must have independent public source calibration before Bind; Bind itself
// neither seals BytecodeSource nor supplies a dispatch/publication permission.
class SourceProducer;
class StoppedCodeReader {
 public:StoppedCodeReader(const StoppedCodeReader&)=delete;StoppedCodeReader&operator=(const StoppedCodeReader&)=delete;
 virtual ~StoppedCodeReader()=default;
 virtual bool Code(void*,void*,CodeView*,const modern_field_metadata::ReadContext&)const noexcept=0;
 private:friend class SourceProducer;StoppedCodeReader()=default;
};
class SourceProducer final:public StoppedCodeReader {
 public:
 bool Bind(HookEngine&,stack_visitor_service::Declaration,const method_identity::Calibration&,
           const modern_field_metadata::FieldMetadata&,void*actual_class_linker,
           void*(*actual_current)(),std::string*,JavaVM* actual_vm=nullptr);
 BytecodeAccess Access()noexcept;
 bool OwnsCurrentThread(void*self)const noexcept{return bound_&&self&&current_&&current_()==self;}
 FrameAccess Frames()noexcept;
 // Read bounded fixed Standard/Compact CodeItem under the same actual current
 // Runnable source callback. This never infers fields from machine code.
 bool Code(void*thread,void*method,CodeView*)const noexcept;
 // Explicit owned stopped/read window; no OS extent fallback. This supplies
 // immutable source bytes only, never an async-frame/publication permission.
 bool Code(void*thread,void*method,CodeView*,const modern_field_metadata::ReadContext&)const noexcept override;
 bool Frame(void*thread,void*method,uint32_t pc,const void**)const noexcept;
 private:
 template<class Base> bool Walk(void*,void*,uint32_t,const void**)const noexcept;
 static bool Current(void*,void*)noexcept;
 static bool GetFrame(void*,void*,void*,uint32_t,const void**)noexcept;
 static bool GetCode(void*,void*,void*,CodeView*)noexcept;
 static bool Resolve(void*,void*,void*,uint32_t,const InvokeInstruction&,void**,void**)noexcept;
 static bool Pending(void*,void*)noexcept;
 static void* CallerDeclaring(void*,void*,void*)noexcept;
 static bool FrameExtent(void*,void*,const void*,size_t*)noexcept;
 static bool FrameReference(void*,void*,uint32_t,void**)noexcept;
 bool CodeImpl(void*,void*,CodeView*,const modern_field_metadata::ReadContext*,evolution::MappedDexMetadata* mapping=nullptr)const noexcept;
 bool Resolve17(void*,void*,uint32_t,const InvokeInstruction&,void**)const noexcept;
 bool Pending17(void*,bool*)const noexcept;
 static bool ReadPending(void*,void*,bool*)noexcept;
 using ResolveMethod=void*(*)(void*,void*,uint32_t,void*,uint32_t);
 stack_visitor_service::Service stack_;stack_visitor_service::Declaration declaration_{};
 code_item_metadata::Layout layout_{};const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 void*linker_=nullptr;void*(*current_)()=nullptr;ResolveMethod resolve_=nullptr;void*(*obsolete_cache_)(void*)=nullptr;
 JavaVM* vm_=nullptr;
 CheckedResolveMethodCurrent resolve_current_=nullptr;
 abi::arm64::android17::ResolveMethodClass resolve_class17_=nullptr;
 abi::arm64::android17::FindResolvedMethod find_method17_=nullptr;
 abi::arm64::android17::CheckedNterpMethod checked_method17_=nullptr;
 abi::arm64::android17::ClearCallerAccessChecks clear_caller17_=nullptr;
 bool bound_=false;
 void*(*root_reference_)(void*)=nullptr;
};
}

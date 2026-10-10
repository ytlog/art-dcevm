#pragma once
#include "platform/common/native_receiver_frame.h"
namespace artdcevm::receiver_guard {
enum class InvokeKind:uint8_t {Virtual,Super,Direct,Static,Interface,Polymorphic,Custom,NonInvoke};
struct InvokeInstruction {InvokeKind kind=InvokeKind::NonInvoke;uint32_t index=0,receiver_register=0,argument_count=0;bool range=false;};
// Instructions and ID counts must come from the actual immutable caller DEX
// CodeItem. The caller's source frame independently supplies the current PC.
struct CodeView {const uint16_t*instructions=nullptr;size_t units=0;uint32_t registers=0,method_ids=0,call_sites=0;};
bool DecodeInvoke(const CodeView&,uint32_t actual_pc,InvokeInstruction*)noexcept;
enum class BytecodeOutcome:uint8_t {Unavailable,NonInvoke,Resolved,OriginalException,GuardedBridge};
struct BytecodeResult {BytecodeOutcome outcome=BytecodeOutcome::Unavailable;void*member=nullptr;void*receiver=nullptr;void*special_caller=nullptr;};
struct BytecodeAccess {
 void*owner=nullptr;
 bool(*current)(void*,void*)noexcept=nullptr;
 // Native named StackVisitor/source owners; never guessed Thread offsets.
 bool(*frame)(void*,void*,void*,uint32_t,const void**)noexcept=nullptr;
 bool(*code)(void*,void*,void*,CodeView*)noexcept=nullptr;
 // Original named source ResolveMethod. It may suspend; Source rereads the
 // actual frame and receiver afterward. Null without actual pending is denied.
 bool(*resolve)(void*,void*,void*,const InvokeInstruction&,void**,void**)noexcept=nullptr;
 bool(*pending)(void*,void*)noexcept=nullptr;
 // invoke-custom/polymorphic remain unavailable until their original source
 // bridge to the sole MH/VH consumer has an independently owned typed proof.
};
struct BytecodeOracle {void*thread=nullptr;void*caller=nullptr;uint32_t pc=0;InvokeInstruction instruction;void*member=nullptr;void*receiver=nullptr;void*special_caller=nullptr;};
class BytecodeSource final {
 public:
 BytecodeSource(const FrameReader&frames,BytecodeAccess access):frames_(frames),access_(access){}
 bool Observe(const BytecodeOracle&)noexcept;
 bool Seal()noexcept;
 bool Proven()const noexcept{return sealed_;}
 bool UsesFrames(const FrameReader&frames)const noexcept{return &frames_==&frames;}
 BytecodeResult Read(void*thread,void*actual_caller,uint32_t actual_pc)const noexcept;
 private:
 BytecodeResult ReadImpl(void*,void*,uint32_t,InvokeInstruction*)const noexcept;
 const FrameReader&frames_;BytecodeAccess access_;uint16_t forms_=0;bool sealed_=false;
};
}

#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::receiver_guard {
// Fixed libdexfile/dex/dex_instruction.h declarations, Android 11–17.
// Virtual calls preserve Range/VarArgs/NoReceiver semantics; operand zero in
// exported MethodHandle/VarHandle entry points is the actual receiver.
class InstructionOperands {
 public:
 explicit InstructionOperands(size_t n):count_(n){}
 virtual ~InstructionOperands()=default;
 virtual uint32_t GetOperand(size_t)const=0;
 size_t Count()const{return count_;}
 private:const size_t count_;
};
static_assert(sizeof(InstructionOperands)==16);
struct FrameAccess {
 void*owner=nullptr;
 bool(*current)(void*,void*)noexcept=nullptr;
 // Independent source-owned frame extent, never an unchecked count-based size.
 bool(*extent)(void*,void*,const void*,size_t*)noexcept=nullptr;
 bool(*reference)(void*,void*,uint32_t,void**)noexcept=nullptr;
};
struct FrameOracle {void*thread=nullptr;void*method=nullptr;void*receiver=nullptr;uint32_t receiver_register=0;};
class FrameReader final {
 public:
 explicit FrameReader(FrameAccess access):access_(access){}
 // Called only from a witnessed real source entry with independently rooted
 // public/native method + receiver and the fixture's exact source register.
 // Ambiguous declarations cannot seal. No API/version selector is accepted.
 bool Observe(const void* frame,const InstructionOperands*,const FrameOracle&)noexcept;
 bool Seal()noexcept;
 bool Read(void*thread,const void*frame,const InstructionOperands*,void**receiver)const noexcept;
 bool Proven()const noexcept{return sealed_;}
 private:
 bool Bounds(void*,const void*,const InstructionOperands*,size_t,size_t,uint32_t*,uint32_t*)const noexcept;
 FrameAccess access_;unsigned candidates_=3;bool observed_=false,sealed_=false;
};
}

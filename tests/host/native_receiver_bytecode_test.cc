#include "platform/common/native_receiver_bytecode.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
namespace r=artdcevm::receiver_guard;
struct State {int thread,caller,target,receiver,relocated,super;std::array<unsigned char,96>frame{};std::array<uint16_t,4>code{};bool current=true,pending=false,fail=false,change_pc=false,move=false;unsigned resolves=0;};
bool Current(void*p,void*t)noexcept{auto&s=*static_cast<State*>(p);return s.current&&t==&s.thread;}
bool Extent(void*p,void*,const void*f,size_t*out)noexcept{auto&s=*static_cast<State*>(p);*out=f==s.frame.data()?s.frame.size():sizeof(r::InstructionOperands);return true;}
bool Reference(void*p,void*,uint32_t raw,void**out)noexcept{auto&s=*static_cast<State*>(p);if(raw!=17&&raw!=19)return false;*out=raw==17?&s.receiver:&s.relocated;return true;}
class Operand:public r::InstructionOperands{public:Operand():InstructionOperands(1){}uint32_t GetOperand(size_t)const override{return 0;}};
bool Frame(void*p,void*,void*,uint32_t,const void**out)noexcept{*out=static_cast<State*>(p)->frame.data();return true;}
bool Code(void*p,void*,void*,r::CodeView*out)noexcept{auto&s=*static_cast<State*>(p);*out={s.code.data(),s.code.size(),4,3,2};return true;}
bool Resolve(void*p,void*,void*,const r::InvokeInstruction&i,void**member,void**special)noexcept{auto&s=*static_cast<State*>(p);++s.resolves;*member=&s.target;*special=i.kind==r::InvokeKind::Super?&s.super:nullptr;if(s.move){uint32_t raw=19;std::memcpy(s.frame.data()+80,&raw,4);}if(s.change_pc){uint32_t pc=1;std::memcpy(s.frame.data()+52,&pc,4);}return !s.fail;}
bool Pending(void*p,void*)noexcept{return static_cast<State*>(p)->pending;}
int main(){
 std::array<uint16_t,4>bytes{};r::CodeView view{bytes.data(),bytes.size(),8,3,2};r::InvokeInstruction decoded;
 for(unsigned kind=0;kind<5;++kind)for(bool range:{false,true}){bytes={uint16_t(((range?0x74:0x6e)+kind)|(3<<(range?8:12))),2,uint16_t(range?4:0x654),0};assert(r::DecodeInvoke(view,0,&decoded));assert(unsigned(decoded.kind)==kind&&decoded.range==range&&decoded.index==2&&decoded.argument_count==3&&decoded.receiver_register==4);}
 auto invalid=[&]{decoded.index=999;assert(!r::DecodeInvoke(view,0,&decoded)&&decoded.index==999);};
 bytes={0x606e,0,0,0};invalid(); // 35c count exceeds five.
 bytes={0x306e,0,0x984,0};invalid(); // Every source operand is bounded.
 bytes={0x0374,0,7,0};invalid(); // Entire range is bounded.
 bytes={0x106e,3,0,0};invalid(); // Actual method ID count.
 bytes={0x006e,0,0,0};invalid(); // Instance receiver is required.
 bytes={0x0100,0,0,0};invalid(); // Payload cannot be an executable PC.
 bytes={0x106e,0,0,0};view.units=2;invalid();view.units=4;
 bytes={0x1071,0,0,0};assert(r::DecodeInvoke(view,0,&decoded));bytes={0,0,0,0};assert(r::DecodeInvoke(view,0,&decoded)&&decoded.kind==r::InvokeKind::NonInvoke);
 State s;void*caller=&s.caller;uint32_t registers=4,raw=17;std::memcpy(s.frame.data()+8,&caller,8);std::memcpy(s.frame.data()+48,&registers,4);std::memcpy(s.frame.data()+80,&raw,4);Operand operands;r::FrameReader frames({&s,Current,Extent,Reference});assert(frames.Observe(s.frame.data(),&operands,{&s.thread,&s.caller,reinterpret_cast<void*>(17),0})&&frames.Seal());r::BytecodeSource source(frames,{&s,Current,Frame,Code,Resolve,Pending});assert(!source.Seal());
 for(unsigned kind=0;kind<5;++kind)for(bool range:{false,true}){unsigned count=kind==3?0:1;s.code={uint16_t(((range?0x74:0x6e)+kind)|(count<<(range?8:12))),0,0,0};r::InvokeInstruction invoke{static_cast<r::InvokeKind>(kind),0,0,count,range};auto oracle=r::BytecodeOracle{&s.thread,&s.caller,0,invoke,&s.target,kind==3?nullptr:&s.receiver,kind==1?&s.super:nullptr};auto wrong=oracle;wrong.member=&s.super;assert(!source.Observe(wrong));assert(source.Observe(oracle));}assert(source.Seal());
 s.code={0x106e,0,0,0};s.move=true;auto result=source.Read(&s.thread,&s.caller,0);assert(result.outcome==r::BytecodeOutcome::Resolved&&result.receiver==&s.relocated);s.move=false;
 s.pending=true;s.fail=true;result=source.Read(&s.thread,&s.caller,0);assert(result.outcome==r::BytecodeOutcome::OriginalException);s.pending=false;assert(source.Read(&s.thread,&s.caller,0).outcome==r::BytecodeOutcome::Unavailable);s.fail=false;
 s.change_pc=true;assert(source.Read(&s.thread,&s.caller,0).outcome==r::BytecodeOutcome::Unavailable);s.change_pc=false;uint32_t pc=0;std::memcpy(s.frame.data()+52,&pc,4);assert(source.Read(&s.thread,&s.target,0).outcome==r::BytecodeOutcome::Unavailable);
 unsigned before=s.resolves;s.code={0x10fa,0,0,0};assert(source.Read(&s.thread,&s.caller,0).outcome==r::BytecodeOutcome::Unavailable&&s.resolves==before);s.code={0x10fc,0,0,0};assert(source.Read(&s.thread,&s.caller,0).outcome==r::BytecodeOutcome::Unavailable&&s.resolves==before);s.current=false;assert(source.Read(&s.thread,&s.caller,0).outcome==r::BytecodeOutcome::Unavailable);
 std::cout<<"Bytecode source: ten actual invoke forms, bounded operands/IDs/PC, exact source oracle, relocated receiver after resolver suspension, original exception, changed frame rejection and unproved bridge denial PASS\n";
}

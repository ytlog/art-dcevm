#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/compiled_frame_dex_oracle.h"
namespace artdcevm::receiver_guard {
bool DecodeInvoke(const CodeView&code,uint32_t pc,InvokeInstruction*out)noexcept{
 if(!out||!code.instructions||pc>=code.units||code.registers>UINT16_MAX)return false;const uint16_t first=code.instructions[pc];const unsigned op=first&255u;
 InvokeInstruction value;unsigned width=compiled_frame_dex_oracle::detail::Width(op);
 if(op==0&&first!=0)return false; // Payloads are never executable source PCs.
 if(!width||width>code.units-pc)return false;
 unsigned ordinal=0;
 if(op>=0x6e&&op<=0x72)ordinal=op-0x6e;
 else if(op>=0x74&&op<=0x78){ordinal=op-0x74;value.range=true;}
 else if(op==0xfa||op==0xfb){value.kind=InvokeKind::Polymorphic;value.range=op==0xfb;}
 else if(op==0xfc||op==0xfd){value.kind=InvokeKind::Custom;value.range=op==0xfd;}
 else {*out=value;return true;}
 if(value.kind==InvokeKind::NonInvoke)value.kind=static_cast<InvokeKind>(ordinal);
 value.index=code.instructions[pc+1];if(value.index>=(value.kind==InvokeKind::Custom?code.call_sites:code.method_ids))return false;
 const uint16_t operands=code.instructions[pc+2];value.argument_count=value.range?first>>8:first>>12;
 if(value.range){value.receiver_register=operands;if(value.argument_count>code.registers||operands>code.registers-value.argument_count)return false;}
 else {if(value.argument_count>5)return false;value.receiver_register=operands&15u;for(uint32_t i=0;i<value.argument_count;++i){const uint32_t reg=i==4?(first>>8)&15u:(operands>>(i*4))&15u;if(reg>=code.registers)return false;}}
 if(value.kind!=InvokeKind::Static&&value.kind!=InvokeKind::Custom&&!value.argument_count)return false;
 *out=value;return true;
}
BytecodeResult BytecodeSource::ReadImpl(void*self,void*caller,uint32_t pc,InvokeInstruction*out)const noexcept{
 BytecodeResult result;if(!frames_.Proven()||!self||!caller||!access_.current||!access_.frame||!access_.code||!access_.resolve||!access_.pending||!access_.current(access_.owner,self))return result;
 const void*frame=nullptr;void*actual=nullptr;uint32_t position=0,registers=0;CodeView code;InvokeInstruction instruction;
 if(!access_.frame(access_.owner,self,caller,pc,&frame)||!frames_.Identity(self,frame,&actual,&position,&registers)||actual!=caller||position!=pc||!access_.code(access_.owner,self,caller,&code)||code.registers!=registers||!DecodeInvoke(code,pc,&instruction))return result;
 if(out)*out=instruction;if(instruction.kind==InvokeKind::NonInvoke){result.outcome=BytecodeOutcome::NonInvoke;return result;}
 if(instruction.kind==InvokeKind::Polymorphic||instruction.kind==InvokeKind::Custom){return result;}
 void*member=nullptr,*special=nullptr;
 const bool resolved=access_.resolve(access_.owner,self,caller,pc,instruction,&member,&special);
 // The source resolver can suspend or throw. Validate current actual frame
 // identity again before borrowing any receiver or caller Class pointer.
 if(!access_.current(access_.owner,self))return result;
 if(access_.pending(access_.owner,self)){result.outcome=BytecodeOutcome::OriginalException;return result;}
 const void*after=nullptr;if(!access_.frame(access_.owner,self,caller,pc,&after)||after!=frame||!frames_.Identity(self,after,&actual,&position,&registers)||actual!=caller||position!=pc)return result;
 if(!resolved||!member||(instruction.kind==InvokeKind::Super?!special:special!=nullptr))return result;
 if(instruction.kind!=InvokeKind::Static&&!frames_.ReadRegister(self,after,instruction.receiver_register,&result.receiver))return result;
 result.outcome=BytecodeOutcome::Resolved;result.member=member;result.special_caller=special;return result;
}
bool BytecodeSource::Observe(const BytecodeOracle&oracle)noexcept{
 if(sealed_||!oracle.thread||!oracle.caller||!oracle.member||oracle.instruction.kind>InvokeKind::Interface)return false;InvokeInstruction actual;
 const auto result=ReadImpl(oracle.thread,oracle.caller,oracle.pc,&actual);
 if(result.outcome!=BytecodeOutcome::Resolved||result.member!=oracle.member||result.receiver!=oracle.receiver||result.special_caller!=oracle.special_caller||actual.kind!=oracle.instruction.kind||actual.range!=oracle.instruction.range||actual.index!=oracle.instruction.index||actual.receiver_register!=oracle.instruction.receiver_register||actual.argument_count!=oracle.instruction.argument_count)return false;
 forms_|=uint16_t{1}<<(static_cast<unsigned>(actual.kind)*2+actual.range);return true;
}
bool BytecodeSource::Seal()noexcept{if(sealed_||forms_!=0x3ffu)return false;sealed_=true;return true;}
BytecodeResult BytecodeSource::Read(void*self,void*caller,uint32_t pc)const noexcept{return sealed_?ReadImpl(self,caller,pc,nullptr):BytecodeResult{};}
}

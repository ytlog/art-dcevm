#include "platform/common/native_receiver_source_calibration.h"
#include "platform/common/native_reference_decoder.h"
#include <cstring>
namespace artdcevm::receiver_guard {
bool SourceCalibration::Stage(void*thread,void*caller,void*target,jobject receiver,jobject special,InvokeKind kind,bool range,const unsigned char*bytes,size_t size){
 if(armed_.load(std::memory_order_acquire)||source_.Proven()||!source_.UsesFrames(frames_)||!roots_.Ready()||!thread||!caller||!target||kind>InvokeKind::Interface||!bytes||size<2||size%2||(kind==InvokeKind::Static?receiver!=nullptr:receiver==nullptr)||(kind==InvokeKind::Super?special==nullptr:special!=nullptr))return false;
 public_code_.resize(size/2);std::memcpy(public_code_.data(),bytes,size);thread_=thread;caller_=caller;target_=target;receiver_=receiver;special_=special;kind_=kind;range_=range;witnessed_form_=0;seen_.store(false,std::memory_order_relaxed);armed_.store(true,std::memory_order_release);return true;
}
void SourceCalibration::DexPcMoved(void*self,void*caller,uint32_t pc)noexcept{
 if(!armed_.load(std::memory_order_acquire)||self!=thread_||caller!=caller_||seen_.load(std::memory_order_relaxed))return;
 const auto&access=source_.access_;if(!access.current||!access.frame||!access.code||!access.current(access.owner,self))return;
 CodeView code;if(!access.code(access.owner,self,caller,&code)||code.units!=public_code_.size()||!code.instructions||std::memcmp(code.instructions,public_code_.data(),code.units*2))return;
 // The source event, not a fabricated PC, identifies an executable instruction.
 // Its complete immutable bytes independently match JVMTI GetBytecodes.
 InvokeInstruction expected;const CodeView public_view{public_code_.data(),public_code_.size(),UINT16_MAX,UINT32_MAX,0};
 if(!DecodeInvoke(public_view,pc,&expected)||expected.kind!=kind_||expected.range!=range_)return;
 const void*frame=nullptr;if(!access.frame(access.owner,self,caller,pc,&frame)||!frame)return;
 if(!frames_.Proven()){
  if(!receiver_)return;void*receiver=nullptr;if(!roots_.StrongRunnable(self,receiver_,&receiver))return;
  class Operand final:public InstructionOperands {public:explicit Operand(uint32_t r):InstructionOperands(1),reg(r){}uint32_t GetOperand(size_t i)const override{return i?UINT32_MAX:reg;}uint32_t reg; }operand(expected.receiver_register);
  if(!frames_.Observe(frame,&operand,{self,caller,receiver,expected.receiver_register})||!frames_.Seal())return;
 }
 InvokeInstruction actual;const auto result=source_.ReadImpl(self,caller,pc,&actual);
 if(result.outcome!=BytecodeOutcome::Resolved||result.member!=target_||actual.kind!=expected.kind||actual.range!=expected.range||actual.index!=expected.index||actual.receiver_register!=expected.receiver_register||actual.argument_count!=expected.argument_count)return;
 // ResolveMethod may allocate/GC: never compare against a managed pointer
 // borrowed in an earlier JNI or pre-resolution source interval.
 void*receiver=nullptr,*special=nullptr;
 if((receiver_&&!roots_.StrongRunnable(self,receiver_,&receiver))||(special_&&!roots_.StrongRunnable(self,special_,&special))||result.receiver!=receiver||result.special_caller!=special||!access.current(access.owner,self))return;
 witnessed_form_=uint16_t{1}<<(unsigned(expected.kind)*2+expected.range);seen_.store(true,std::memory_order_release);
}
bool SourceCalibration::Finish(bool success)noexcept{
 if(!armed_.exchange(false,std::memory_order_acq_rel))return false;
 const bool passed=success&&seen_.load(std::memory_order_acquire)&&witnessed_form_;
 const auto witnessed=witnessed_form_;
 // The same-thread public invocation has returned. Retain only the witnessed
 // form, never its borrowed Thread/Method identities or temporary JNI roots.
 thread_=caller_=target_=nullptr;receiver_=special_=nullptr;public_code_.clear();
 kind_=InvokeKind::NonInvoke;range_=false;witnessed_form_=0;
 seen_.store(false,std::memory_order_relaxed);
 if(!passed)return false;
 forms_|=witnessed;source_.forms_|=witnessed;return true;
}
bool SourceCalibration::Seal()noexcept{if(sealed_.load(std::memory_order_acquire)||forms_!=0x3ffu||armed_.load(std::memory_order_acquire)||!source_.Seal())return false;sealed_.store(true,std::memory_order_release);return true;}
}

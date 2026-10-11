#include "platform/common/native_call_graph_code.h"
#include "platform/common/native_dex_instruction_width.h"
namespace artdcevm::native_call_graph {
bool ScanCode(const receiver_guard::CodeView&code,std::vector<CallSite>*out,const char**failure){
 if(failure)*failure=nullptr;
 auto deny=[&](const char*reason){if(out)out->clear();if(failure)*failure=reason;return false;};
 if(!out||!code.instructions||!code.units||code.units>UINT32_MAX||code.registers>UINT16_MAX)
  return deny("call-graph-code-unavailable");
 std::vector<CallSite> result;
 for(uint32_t pc=0;pc<code.units;){
  const uint16_t word=code.instructions[pc];const unsigned op=word&255u;
  uint64_t width=dex_instruction::Width(op);
  if(op==0&&word){
   if((pc&1)||code.units-pc<2)return deny("call-graph-payload-header");
   const unsigned kind=word>>8;
   if(kind==1)width=4ull+uint64_t(code.instructions[pc+1])*2;
   else if(kind==2)width=2ull+uint64_t(code.instructions[pc+1])*4;
   else if(kind==3){
    if(code.units-pc<4)return deny("call-graph-array-payload-header");
    const unsigned element=code.instructions[pc+1];
    if(element!=1&&element!=2&&element!=4&&element!=8)return deny("call-graph-array-element-width");
    const uint32_t count=uint32_t(code.instructions[pc+2])|(uint32_t(code.instructions[pc+3])<<16);
    width=4+(uint64_t(element)*count+1)/2;
   }else return deny("call-graph-payload-kind");
  }else{
   if(!width||width>code.units-pc)return deny("call-graph-instruction-width");
   receiver_guard::InvokeInstruction instruction;
   if(!receiver_guard::DecodeInvoke(code,pc,&instruction))return deny("call-graph-invoke-operands");
   if(instruction.kind!=receiver_guard::InvokeKind::NonInvoke)result.push_back({pc,instruction});
  }
  if(!width||width>code.units-pc)return deny("call-graph-payload-size");
  pc+=static_cast<uint32_t>(width);
 }
 *out=std::move(result);return true;
}
}

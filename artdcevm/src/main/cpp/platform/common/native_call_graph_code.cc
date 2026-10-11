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
  uint32_t width=0;bool payload=false;
  if(!dex_instruction::Step(code.instructions,code.units,pc,&width,&payload))
   return deny("call-graph-instruction-or-payload-span");
  if(!payload){
   receiver_guard::InvokeInstruction instruction;
   if(!receiver_guard::DecodeInvoke(code,pc,&instruction))return deny("call-graph-invoke-operands");
   if(instruction.kind!=receiver_guard::InvokeKind::NonInvoke)result.push_back({pc,instruction});
  }
  pc+=width;
 }
 *out=std::move(result);return true;
}
}

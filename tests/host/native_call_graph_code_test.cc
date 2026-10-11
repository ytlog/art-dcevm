#include "platform/common/native_call_graph_code.h"
#include "platform/common/native_dex_instruction_width.h"
#include <cassert>
#include <cstdio>
using namespace artdcevm;
int main(){
 using receiver_guard::InvokeKind;
 // A const operand contains an invoke opcode. Only PC 3 is an actual call.
 uint16_t body[]={0x0014,0x1071,0,0x1071,2,0,0x000e};
 receiver_guard::CodeView code;code.instructions=body;code.units=7;code.registers=2;code.method_ids=3;
 std::vector<native_call_graph::CallSite> sites;const char*failure=nullptr;
 assert(native_call_graph::ScanCode(code,&sites,&failure)&&!failure&&sites.size()==1);
 assert(sites[0].dex_pc==3&&sites[0].instruction.kind==InvokeKind::Static&&sites[0].instruction.index==2);
 // Packed payload entries resemble invoke-static; none are executable calls.
 uint16_t payload[]={0x0100,1,0,0,0x1071,2,0x0178,1,0,0x000e};
 code.instructions=payload;code.units=10;
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==6&&sites[0].instruction.kind==InvokeKind::Interface);
 // Sparse keys and targets may resemble invokes. Both arrays are payload
 // data; the following real invoke must survive at the exact width-derived PC.
 uint16_t sparse[]={0x0200,2,0x1071,0,0xffff,0xffff,0x0178,1,0x10fa,0,0x1071,1,0,0x000e};
 code.instructions=sparse;code.units=14;
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==10);
 uint32_t width=99;bool is_payload=false;
 assert(dex_instruction::Step(sparse,14,0,&width,&is_payload)&&width==10&&is_payload);
 // A two-entry packed table includes a signed first key and call-like targets.
 uint16_t packed[]={0x0100,2,0xffff,0xffff,0x1071,1,0x0178,2,0x1071,1,0,0x000e};
 code.instructions=packed;code.units=12;
 assert(dex_instruction::Step(packed,12,0,&width,&is_payload)&&width==8&&is_payload);
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==8);
 // Odd byte count rounds up to one final data code unit. The padding byte is
 // data too, even when that unit looks like an invoke-polymorphic opcode.
 uint16_t fill[]={0x0300,1,5,0,0x1071,0x0178,0x10fa,0x1071,1,0,0x000e};
 code.instructions=fill;code.units=11;
 assert(dex_instruction::Step(fill,11,0,&width,&is_payload)&&width==7&&is_payload);
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==7);
 for(uint16_t element:{uint16_t{2},uint16_t{4},uint16_t{8}}){
  std::vector<uint16_t> data(4+element+4,0);
  data[0]=0x0300;data[1]=element;data[2]=2;data[4]=0x1071;
  const size_t call_pc=4+element;data[call_pc]=0x1071;data[call_pc+1]=1;data[call_pc+3]=0x000e;
  code.instructions=data.data();code.units=data.size();
  assert(dex_instruction::Step(data.data(),data.size(),0,&width,&is_payload)&&width==call_pc&&is_payload);
  assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==call_pc);
 }
 // Payload starts must be 32-bit aligned in the original DEX code-unit stream.
 uint16_t odd[]={0x0000,0x0200,0,0x000e};code.instructions=odd;code.units=4;
 width=99;is_payload=false;
 assert(!dex_instruction::Step(odd,4,1,&width,&is_payload)&&width==99&&!is_payload);
 assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());
 // Declared payload spans cannot borrow from the next instruction or past the
 // end. Failed Step preserves outputs; failed Scan clears previously found calls.
 for(auto stream:{sparse,packed,fill}){
  const size_t declared=stream==sparse?10:stream==packed?8:7;
  width=99;is_payload=false;
  assert(!dex_instruction::Step(stream,declared-1,0,&width,&is_payload)&&width==99&&!is_payload);
  code.instructions=stream;code.units=declared-1;
  assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());
 }
 uint16_t short_fill[]={0x0300,8,0xffff,0xffff};
 assert(!dex_instruction::Step(short_fill,4,0,&width,&is_payload));
 uint16_t unknown_payload[]={0x0400,0};
 assert(!dex_instruction::Step(unknown_payload,2,0,&width,&is_payload));
 uint16_t invalid_fill[]={0x0300,0,0,0};
 assert(!dex_instruction::Step(invalid_fill,4,0,&width,&is_payload));
 invalid_fill[1]=3;assert(!dex_instruction::Step(invalid_fill,4,0,&width,&is_payload));
 uint16_t truncated[]={0x0018,0,0,0}; // const-wide requires five units.
 assert(!dex_instruction::Step(truncated,4,0,&width,&is_payload));
 assert(!dex_instruction::Step(packed,12,12,&width,&is_payload));
 assert(!dex_instruction::Step(nullptr,12,0,&width,&is_payload));
 // Dynamic invokes are preserved for the graph owner to reject or resolve.
 uint16_t dynamic[]={0x10fa,1,0,0,0x00fc,0,0,0x000e};
 code.instructions=dynamic;code.units=8;code.call_sites=1;
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==2);
 assert(sites[0].instruction.kind==InvokeKind::Polymorphic&&sites[1].instruction.kind==InvokeKind::Custom);
 code.units=6;assert(!native_call_graph::ScanCode(code,&sites,&failure)&&failure&&sites.empty());
 code.instructions=body;code.units=7;code.method_ids=2;
 assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());
 code.method_ids=3;body[5]=3;assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());body[5]=0;
 uint16_t malformed[]={0x0300,3,1,0,0};code.instructions=malformed;code.units=5;
 assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());
 malformed[1]=8;malformed[2]=0xffff;malformed[3]=0xffff;
 assert(!native_call_graph::ScanCode(code,&sites)&&sites.empty());
 // Full linear scan includes a call in a handler/unreachable tail.
 uint16_t tail[]={0x000e,0x1070,1,0,0x000e};code.instructions=tail;code.units=5;
 assert(native_call_graph::ScanCode(code,&sites)&&sites.size()==1&&sites[0].dex_pc==1);
 std::puts("Call graph source CodeView instruction/payload/operand/dynamic/handler scan PASS");
}

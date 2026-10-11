#include "platform/common/native_call_graph_code.h"
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

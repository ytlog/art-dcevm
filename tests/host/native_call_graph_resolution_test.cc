#include "platform/common/native_call_graph_resolution.h"
#include <cassert>
#include <iostream>
using namespace artdcevm;
struct Fixture {
 uint16_t words[4]={0x1071,1,0,0x000e};
 receiver_guard::CodeView code{};bool pending=false,mutate=false,current=true;unsigned resolves=0;
 Fixture(){code.instructions=words;code.units=4;code.registers=1;code.method_ids=3;code.source_dex.begin=reinterpret_cast<const unsigned char*>(this);code.code_item=words;}
 receiver_guard::BytecodeAccess Access(){return {this,
 [](void*p,void*)noexcept{return static_cast<Fixture*>(p)->current;},nullptr,
 [](void*p,void*,void*,receiver_guard::CodeView*out)noexcept{*out=static_cast<Fixture*>(p)->code;return true;},
 [](void*p,void*,void*,uint32_t,const receiver_guard::InvokeInstruction&,void**target,void**special)noexcept{
 auto&f=*static_cast<Fixture*>(p);++f.resolves;*target=&f.resolves;*special=nullptr;if(f.mutate)f.words[1]=2;return true;},nullptr};}
 std::vector<unsigned char> Bytes(){const auto*p=reinterpret_cast<const unsigned char*>(words);return {p,p+sizeof(words)};}
 native_call_graph::CallSite Site(){receiver_guard::InvokeInstruction i{};assert(receiver_guard::DecodeInvoke(code,0,&i));return {0,i};}
 static bool Pending(void*p)noexcept{return static_cast<Fixture*>(p)->pending;}
};
int main(){using namespace native_call_graph_resolution;
 Fixture f;auto staged=f.code;auto bytes=f.Bytes();auto site=f.Site();
 auto run=[&](){return detail::Resolve(f.Access(),&f,&f,staged,bytes,site,&f,Fixture::Pending);};
 auto r=run();assert(r.status==Status::Resolved&&r.declaration==&f.resolves&&r.dex_namespace==staged.source_dex.begin);
 site.dex_pc=1;assert(run().status==Status::Unavailable&&f.resolves==1);site=f.Site();
 f.pending=true;assert(run().status==Status::Unavailable&&f.resolves==1);f.pending=false;
 f.mutate=true;assert(run().status==Status::Unavailable&&f.resolves==2);f.words[1]=1;f.mutate=false;
 auto a=f.Access();a.resolve=[](void*p,void*,void*,uint32_t,const receiver_guard::InvokeInstruction&,void**out,void**)noexcept{static_cast<Fixture*>(p)->pending=true;*out=nullptr;return false;};
 r=detail::Resolve(a,&f,&f,staged,bytes,site,&f,Fixture::Pending);assert(r.status==Status::OriginalException&&f.pending&&!r.declaration);
 f.pending=false;
 a=f.Access();a.resolve=[](void*,void*,void*,uint32_t,const receiver_guard::InvokeInstruction&,void**out,void**)noexcept{*out=nullptr;return false;};
 assert(detail::Resolve(a,&f,&f,staged,bytes,site,&f,Fixture::Pending).status==Status::Unavailable);
 site.instruction.kind=receiver_guard::InvokeKind::Custom;assert(run().status==Status::Unavailable);site=f.Site();
 const auto original_namespace=f.code.source_dex.begin;f.code.source_dex.begin=bytes.data();assert(run().status==Status::Unavailable);f.code.source_dex.begin=original_namespace;
 for(uint8_t opcode:{uint8_t(0x6e),uint8_t(0x6f),uint8_t(0x70),uint8_t(0x71),uint8_t(0x72)}){
  f.words[0]=uint16_t(0x1000|opcode);staged=f.code;bytes=f.Bytes();site=f.Site();assert(run().status==Status::Resolved);
 }
 f.current=false;assert(run().status==Status::Unavailable);
 std::cout<<"Original invoke resolution mechanics: PASS\n";
}

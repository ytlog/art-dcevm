#include "platform/common/native_receiver_frame.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm::receiver_guard;
struct Operands:InstructionOperands {explicit Operands(uint32_t i):InstructionOperands(1),index(i){}uint32_t GetOperand(size_t i)const override{assert(i==0);return index;}uint32_t index;};
struct State{void*self;const void*frame;size_t bytes;const void*operands;bool mapped=true;unsigned barriers=0;};
static bool Current(void*p,void*t)noexcept{return static_cast<State*>(p)->self==t;}
static bool Extent(void*p,void*,const void*address,size_t*bytes)noexcept{auto&s=*static_cast<State*>(p);if(!s.mapped)return false;if(address==s.frame)*bytes=s.bytes;else if(address==s.operands)*bytes=sizeof(Operands);else return false;return true;}
static bool Root(void*p,void*,uint32_t value,void**out)noexcept{auto&s=*static_cast<State*>(p);++s.barriers;*out=reinterpret_cast<void*>(uintptr_t(value));return value==0x1000||value==0x2000;}
int main(){for(auto offsets:{std::pair<size_t,size_t>{48,64},{24,36}}){int self,method;std::array<unsigned char,112> frame{};Operands operands(2);State state{&self,frame.data(),frame.size(),&operands};FrameReader reader({&state,Current,Extent,Root});void*method_pointer=&method;std::memcpy(frame.data()+8,&method_pointer,8);uint32_t count=4,root=0x1000;std::memcpy(frame.data()+offsets.first,&count,4);std::memcpy(frame.data()+offsets.second+(count+2)*4,&root,4);
 assert(!reader.Read(&self,frame.data(),&operands,&method_pointer));assert(!reader.Observe(frame.data(),&operands,{&self,&method,reinterpret_cast<void*>(0x3000),2}));assert(!reader.Seal());assert(!state.barriers);
 assert(reader.Observe(frame.data(),&operands,{&self,&method,reinterpret_cast<void*>(0x1000),2}));assert(reader.Seal());void*receiver=nullptr;assert(reader.Read(&self,frame.data(),&operands,&receiver)&&receiver==reinterpret_cast<void*>(0x1000));
 root=0x2000;std::memcpy(frame.data()+offsets.second+(count+2)*4,&root,4);assert(reader.Read(&self,frame.data(),&operands,&receiver)&&receiver==reinterpret_cast<void*>(0x2000));
 operands.index=4;assert(!reader.Read(&self,frame.data(),&operands,&receiver));operands.index=2;state.bytes=offsets.second+31;assert(!reader.Read(&self,frame.data(),&operands,&receiver));state.bytes=frame.size();state.mapped=false;assert(!reader.Read(&self,frame.data(),&operands,&receiver));state.mapped=true;int foreign;assert(!reader.Read(&foreign,frame.data(),&operands,&receiver));
 root=0;std::memcpy(frame.data()+offsets.second+(count+2)*4,&root,4);assert(reader.Read(&self,frame.data(),&operands,&receiver)&&!receiver);
 }
 std::puts("Source ShadowFrame receiver: independently selected 64/36 vreg declarations, actual virtual operand0, exact method/root before candidate decode, null/moved root, bounds/context/extent refusal PASS; live source fixture proof remains required");}

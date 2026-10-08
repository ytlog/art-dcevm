#include "platform/common/owned_context_consumer.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::owned_context_consumer;
namespace {
int copies=0,destroys=0;
class MockContext final:public abi::Context {
 public:
 ~MockContext()override{assert(copies==destroys+1);++destroys;}
 void Reset()override{assert(false);}
 void FillCalleeSaves(uint8_t*,const abi::QuickMethodFrameInfo&)override{assert(false);}
 void SetSP(uintptr_t)override{assert(false);}void SetPC(uintptr_t)override{assert(false);}
 void SetArg0(uintptr_t)override{assert(false);}bool IsAccessibleGPR(uint32_t)override{assert(false);return false;}
 uintptr_t* GetGPRAddress(uint32_t)override{assert(false);return nullptr;}
 uintptr_t GetGPR(uint32_t)override{assert(false);return 0;}void SetGPR(uint32_t,uintptr_t)override{assert(false);}
 bool IsAccessibleFPR(uint32_t)override{assert(false);return false;}
 uintptr_t GetFPR(uint32_t)override{assert(false);return 0;}void SetFPR(uint32_t,uintptr_t)override{assert(false);}
 void SmashCallerSaves()override{assert(false);}
 void CopyContextTo(uintptr_t* g,uintptr_t* f)override{
  assert(copies==destroys);++copies;
  for(size_t i=0;i<kGprSlots;++i)g[i]=0x10000+i;
  for(size_t i=0;i<kFprSlots;++i)f[i]=0x20000+i;
 }
};
struct alignas(16) Buffers {uint64_t before=0xcafebabe;uint64_t padding=0;uintptr_t gprs[34]{};uintptr_t fprs[32]{};uint64_t after=0xbadc0ffee;};
}
int main(){
 ThreadRefreshDeclaration declaration{64,512,MarkingRegister::kReserved};assert(declaration.Valid());
 const uint32_t payload=declaration.Payload();assert((payload&0xffff)==64&&payload>>16==512);
 for(int i=0;i<1000;++i){
  Buffers buffers;
  if(i%2)CopyAndDestroy(abi::OwnedContext(new MockContext),payload,buffers.gprs,buffers.fprs);
  else ArtDcevmCopyContextForLongJump(new MockContext,buffers.gprs,buffers.fprs,payload);
  assert(copies==i+1&&destroys==i+1);
  for(size_t j=0;j<34;++j)assert(buffers.gprs[j]==(j==32?payload:0x10000+j));
  for(size_t j=0;j<32;++j)assert(buffers.fprs[j]==0x20000+j);
  assert(buffers.before==0xcafebabe&&buffers.after==0xbadc0ffee);
 }
 declaration.marking_register=MarkingRegister::kNotReserved;assert(declaration.Valid());
 declaration.gc_marking_offset=65;assert(!declaration.Valid());
 declaration.gc_marking_offset=64;declaration.suspend_trigger_offset=513;assert(!declaration.Valid());
 declaration.suspend_trigger_offset=0;assert(!declaration.Valid());
 declaration.suspend_trigger_offset=512;declaration.marking_register=static_cast<MarkingRegister>(99);assert(!declaration.Valid());
 std::cout<<"Owned Context consumer: full native virtual copy, exact-once destruction, buffers and refresh payload passed\n";
}

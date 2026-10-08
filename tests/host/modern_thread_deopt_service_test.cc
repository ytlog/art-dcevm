#define ARTDCEVM_TESTING
#include "platform/common/modern_thread_deopt_service.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace artdcevm::modern_provider {
// Standalone mock issuer, never linked into the Android library.
class OwnedThreadCalibration {
 public:
 static modern_thread_deopt_service::CalibratedThreadToken Issue(
     void* runtime,void* thread,thread_refresh::SourceDeclaration source,
     owned_context_consumer::MarkingRegister mode,uint64_t generation,
     modern_thread_deopt_service::RequestEligibility eligible){
  return {runtime,thread,source,mode,generation,eligible};
 }
};
}
namespace artdcevm::modern_thread_deopt_service {
class ServiceTestPeer {
 public:
 static bool Run(const Service& svc,void* runtime,void* thread,deoptimize_if_needed_abi::JValue value,
                 bool ref,deoptimize_if_needed_abi::MethodType type,
                 void (*consume)(deoptimize_if_needed_abi::OwnedContext,const consumer::ThreadRefreshDeclaration&)){
  return svc.Run(runtime,thread,value,ref,type,consume);
 }
};
}
namespace {
using namespace modern_thread_deopt_service;
int objects[3];int phase=0,destroys=0,eligibility_calls=0;bool eligible=true;
deoptimize_if_needed_abi::JValue expected_value{};bool expected_ref;deoptimize_if_needed_abi::MethodType expected_type;
thread_refresh::SourceDeclaration expected_source;consumer::MarkingRegister expected_mode;
struct JumpTaken{};
class MockContext final:public deoptimize_if_needed_abi::Context {
 public:
 ~MockContext()override{assert(phase==4);++destroys;}
 void Reset()override{assert(false);}void FillCalleeSaves(uint8_t*,const deoptimize_if_needed_abi::QuickMethodFrameInfo&)override{assert(false);}
 void SetSP(uintptr_t)override{assert(false);}void SetPC(uintptr_t)override{assert(false);}void SetArg0(uintptr_t)override{assert(false);}
 bool IsAccessibleGPR(uint32_t)override{assert(false);return false;}uintptr_t* GetGPRAddress(uint32_t)override{assert(false);return nullptr;}
 uintptr_t GetGPR(uint32_t)override{assert(false);return 0;}void SetGPR(uint32_t,uintptr_t)override{assert(false);}
 bool IsAccessibleFPR(uint32_t)override{assert(false);return false;}uintptr_t GetFPR(uint32_t)override{assert(false);return 0;}
 void SetFPR(uint32_t,uintptr_t)override{assert(false);}void SmashCallerSaves()override{assert(false);}
 void CopyContextTo(uintptr_t* g,uintptr_t* f)override{assert(phase==3);phase=4;for(int i=0;i<34;++i)g[i]=100+i;for(int i=0;i<32;++i)f[i]=200+i;}
};
bool Eligibility(void* runtime,void* thread,uint64_t generation){assert(generation==1);assert(runtime==&objects[0]&&thread==&objects[1]);++eligibility_calls;return eligible;}
void Push(void* thread,const deoptimize_if_needed_abi::JValue& value,bool ref,thread_deoptimize_abi::NullThrowablePtr exception,bool from_code,deoptimize_if_needed_abi::MethodType type){
 assert(thread==&objects[1]&&phase==0);assert(value.j==expected_value.j&&ref==expected_ref&&type==expected_type);
 assert(exception.reference==0&&!from_code);phase=1;
}
deoptimize_if_needed_abi::OwnedContext Deoptimize(void* thread,thread_deoptimize_abi::Kind kind,bool single,bool skip){
 assert(thread==&objects[1]&&phase==1&&static_cast<int>(kind)==8&&!single&&!skip);phase=2;
 return deoptimize_if_needed_abi::OwnedContext(new MockContext);
}
void Consume(deoptimize_if_needed_abi::OwnedContext context,const consumer::ThreadRefreshDeclaration& declaration){
 assert(context&&phase==2);thread_refresh::SourceLayout layout{};assert(thread_refresh::SourceLayoutFor(expected_source,&layout));
 assert(declaration.gc_marking_offset==layout.gc_marking_offset&&declaration.suspend_trigger_offset==layout.suspend_trigger_offset&&declaration.marking_register==expected_mode);
 phase=3;alignas(16) uintptr_t g[34],f[32];consumer::CopyAndDestroy(std::move(context),declaration.Payload(),g,f);
 assert(phase==4&&g[32]==declaration.Payload()&&g[33]==133&&f[31]==231);phase=5;throw JumpTaken{};
}
}
int main(){
 for(auto source:{thread_refresh::SourceDeclaration::kAndroid16,thread_refresh::SourceDeclaration::kAndroid17})
 for(auto mode:{consumer::MarkingRegister::kNotReserved,consumer::MarkingRegister::kReserved}){
  expected_source=source;expected_mode=mode;
  auto token=modern_provider::OwnedThreadCalibration::Issue(&objects[0],&objects[1],source,mode,1,Eligibility);
  Service service;int lookups=0;
  auto lookup=[&](const char* name)->void*{++lookups;
   if(std::strcmp(name,thread_deoptimize_abi::kPushContext)==0)return reinterpret_cast<void*>(Push);
   assert(std::strcmp(name,thread_deoptimize_abi::kDeoptimize)==0);return reinterpret_cast<void*>(Deoptimize);
  };
  assert(Service::Bind(token,&objects[0],lookup,&service)&&lookups==2);
  for(auto type:{deoptimize_if_needed_abi::MethodType::kKeepDexPc,deoptimize_if_needed_abi::MethodType::kDefault})for(bool ref:{false,true}){
   phase=0;expected_type=type;expected_ref=ref;expected_value.j=INT64_C(0x1234567812345678);int before=destroys;
   try{ServiceTestPeer::Run(service,&objects[0],&objects[1],expected_value,ref,type,Consume);assert(false);}catch(JumpTaken&){}
   assert(phase==5&&destroys==before+1);
  }
  phase=0;int before=eligibility_calls;eligible=false;
  assert(!ServiceTestPeer::Run(service,&objects[0],&objects[1],expected_value,false,deoptimize_if_needed_abi::MethodType::kDefault,Consume));
  assert(phase==0&&eligibility_calls==before+1);eligible=true;
  before=eligibility_calls;
  assert(!ServiceTestPeer::Run(service,&objects[2],&objects[1],expected_value,false,deoptimize_if_needed_abi::MethodType::kDefault,Consume));
  assert(!ServiceTestPeer::Run(service,&objects[0],&objects[2],expected_value,false,deoptimize_if_needed_abi::MethodType::kDefault,Consume));
  assert(!ServiceTestPeer::Run(service,&objects[0],&objects[1],expected_value,false,static_cast<deoptimize_if_needed_abi::MethodType>(99),Consume));
  assert(phase==0&&eligibility_calls==before);
  int failed=0;
  assert(!Service::Bind(token,&objects[0],[&](const char*)->void*{++failed;return nullptr;},&service)&&failed==1);
  assert(!Service::Bind(token,&objects[0],[&](const char* name)->void*{return std::strcmp(name,thread_deoptimize_abi::kPushContext)==0?reinterpret_cast<void*>(Push):nullptr;},&service));
  assert(!Service::Bind(token,&objects[2],lookup,&service)&&lookups==2);
  assert(!Service::Bind(token,&objects[0],lookup,nullptr)&&lookups==2);
  CalibratedThreadToken invalid;
  assert(!Service::Bind(invalid,&objects[0],lookup,&service)&&lookups==2);
  auto stale=modern_provider::OwnedThreadCalibration::Issue(&objects[0],&objects[1],source,mode,0,Eligibility);
  assert(!Service::Bind(stale,&objects[0],lookup,&service)&&lookups==2);
  for(auto invalid_source:{static_cast<thread_refresh::SourceDeclaration>(99)}){
   auto bad=modern_provider::OwnedThreadCalibration::Issue(&objects[0],&objects[1],invalid_source,mode,1,Eligibility);
   assert(!Service::Bind(bad,&objects[0],lookup,&service)&&lookups==2);
  }
  auto bad_mode=modern_provider::OwnedThreadCalibration::Issue(&objects[0],&objects[1],source,static_cast<consumer::MarkingRegister>(99),1,Eligibility);
  assert(!Service::Bind(bad_mode,&objects[0],lookup,&service)&&lookups==2);
  auto no_oracle=modern_provider::OwnedThreadCalibration::Issue(&objects[0],&objects[1],source,mode,1,nullptr);
  assert(!Service::Bind(no_oracle,&objects[0],lookup,&service)&&lookups==2);
  auto no_subject=modern_provider::OwnedThreadCalibration::Issue(&objects[0],nullptr,source,mode,1,Eligibility);
  assert(!Service::Bind(no_subject,&objects[0],lookup,&service)&&lookups==2);
  phase=0;
  try{ServiceTestPeer::Run(service,&objects[0],&objects[1],expected_value,expected_ref,expected_type,Consume);assert(false);}catch(JumpTaken&){}
  assert(phase==5); // Failed rebind preserved the full previously bound service.
 }
 std::cout<<"Modern Thread deopt: owned mock token, transactional named binding, exact push/full-stack call, ownership transfer and eligibility rejection passed\n";
}

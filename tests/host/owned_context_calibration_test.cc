#include "platform/common/owned_context_calibration.h"
#include <cassert>
#include <cstdio>
using namespace artdcevm::owned_context_calibration;
using Prefix=artdcevm::abi::arm64::android_common::ContextPrefix;
int created=0,destroyed=0,mode=0;
class Context final:public Prefix {
 public:
 Context(){++created;Reset();}
 ~Context()override{++destroyed;}
 void Reset()override{arg=0;sp=0xebad6070+31;pc=0xebad6070+33;if(mode==1)++sp;}
 void FillCalleeSaves(uint8_t*,const artdcevm::abi::arm64::android_common::QuickMethodFrameInfo&)override{assert(false);}
 void SetSP(uintptr_t value)override{sp=mode==2?uint32_t(value):value;}
 void SetPC(uintptr_t value)override{pc=value;}
 void SetArg0(uintptr_t value)override{arg=value;}
 bool IsAccessibleGPR(uint32_t reg)override{return reg==0||reg==31||reg==33;}
 uintptr_t* GetGPRAddress(uint32_t reg)override{return reg==0?&arg:reg==31?&sp:reg==33?&pc:nullptr;}
 uintptr_t GetGPR(uint32_t reg)override{assert(reg<33);return *GetGPRAddress(reg);}
 private:uintptr_t arg=0,sp=0,pc=0;
};
Prefix* Create(){return new Context;}
Prefix* Null(){return nullptr;}
int main(){
 Service service;Report report;
 assert(!service.Run(1,&report)&&report.failure==Failure::kUnavailable);
 assert(Service::Bind(reinterpret_cast<void*>(Create),&service));
 assert(!Service::Bind(nullptr,&service));
 assert(service.Run(128,&report)&&report.completed==128&&report.checks==384);
 assert(created==128&&destroyed==128);
 mode=1;assert(!service.Run(1,&report)&&report.failure==Failure::kResetModel);assert(created==destroyed);
 mode=2;assert(!service.Run(1,&report)&&report.failure==Failure::kRegisterWrite);assert(created==destroyed);
 mode=0;assert(!service.Run(0,&report)&&report.failure==Failure::kInvalidRequest);
 assert(!service.Run(4097,&report)&&report.failure==Failure::kInvalidRequest);
 assert(!service.Run(1,nullptr));
 assert(Service::Bind(reinterpret_cast<void*>(Null),&service));assert(!service.Run(1,&report)&&report.failure==Failure::kAllocation);
 puts("Owned Context calibration: independent storage, 64-bit writes, reset, native virtual destruction on success/failure and invalid requests passed");
}

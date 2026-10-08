#include "platform/common/owned_context_calibration.h"
#include <memory>
namespace artdcevm::owned_context_calibration {
bool Service::Bind(void* named_create,Service* output){
  if(!named_create||!output)return false;
  Service candidate;candidate.create_=reinterpret_cast<decltype(create_)>(named_create);
  *output=candidate;return true;
}
bool Service::Run(uint32_t iterations,Report* output)const{
  if(!output)return false;
  Report report;
  auto fail=[&](Failure failure){report.failure=failure;*output=report;return false;};
  if(!iterations||iterations>4096)return fail(Failure::kInvalidRequest);
  if(!create_)return fail(Failure::kUnavailable);
  constexpr uint32_t sp=31,pc=33,arg0=0;
  constexpr uintptr_t bad=0xebad6070;
  for(uint32_t i=0;i<iterations;++i){
    std::unique_ptr<abi::arm64::android_common::ContextPrefix> context(create_());
    if(!context)return fail(Failure::kAllocation);
    // Reset allocates no Thread/stack references. Only independently owned
    // register cells populated by Reset are read; inaccessible GPRs are not read.
    context->Reset();
    if(!context->IsAccessibleGPR(arg0)||!context->IsAccessibleGPR(sp)||
       !context->IsAccessibleGPR(pc)||context->IsAccessibleGPR(1))return fail(Failure::kResetModel);
    auto* a=context->GetGPRAddress(arg0);
    auto* s=context->GetGPRAddress(sp);
    auto* p=context->GetGPRAddress(pc);
    if(!a||!s||!p||a==s||a==p||s==p||*a!=0||*s!=bad+sp||*p!=bad+pc)
      return fail(Failure::kResetModel);
    ++report.checks;
    const uintptr_t arg_value=uintptr_t{0x123456789abc0000}+i;
    const uintptr_t sp_value=uintptr_t{0x3456789abcde0000}+i*16;
    const uintptr_t pc_value=uintptr_t{0x56789abcdef00000}+i*4;
    context->SetArg0(arg_value);context->SetSP(sp_value);context->SetPC(pc_value);
    // PC is intentionally read only through GetGPRAddress; native GetGPR
    // excludes the extra PC cell and must never receive that register index.
    if(*a!=arg_value||*s!=sp_value||*p!=pc_value||
       context->GetGPR(arg0)!=arg_value||context->GetGPR(sp)!=sp_value)
      return fail(Failure::kRegisterWrite);
    ++report.checks;
    context->Reset();
    if(*a!=0||*s!=bad+sp||*p!=bad+pc||context->IsAccessibleGPR(1))
      return fail(Failure::kResetRestore);
    ++report.checks;
    // Count completion after the native deleting destructor has returned.
    context.reset();++report.completed;
  }
  *output=report;return true;
}
}

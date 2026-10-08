#include "platform/common/owned_context_consumer.h"
#include <cstdlib>
namespace artdcevm::owned_context_consumer {
void CopyAndDestroy(abi::OwnedContext context,uint32_t payload,
                    uintptr_t (&gprs)[kGprSlots],uintptr_t (&fprs)[kFprSlots]){
  if(!context)std::abort();
  context->CopyContextTo(gprs,fprs);
  // The fixed Context::CopyContextTo writes 34 native GPR slots. XZR is never
  // restored; the consumer uses it only after the platform copy has completed.
  gprs[32]=payload;
  // canonical unique_ptr destruction calls the native virtual deleting dtor.
}
#if defined(__aarch64__)
extern "C" [[noreturn]] void ArtDcevmArm64LongJumpReserved(abi::Context*,uint32_t);
extern "C" [[noreturn]] void ArtDcevmArm64LongJumpUnreserved(abi::Context*,uint32_t);
[[noreturn]] void Jump(abi::OwnedContext context,const ThreadRefreshDeclaration& declaration){
  if(!context||!declaration.Valid())std::abort();
  const uint32_t payload=declaration.Payload();
  if(declaration.marking_register==MarkingRegister::kReserved)
    ArtDcevmArm64LongJumpReserved(context.release(),payload);
  ArtDcevmArm64LongJumpUnreserved(context.release(),payload);
}
#endif
}  // namespace artdcevm::owned_context_consumer
extern "C" void ArtDcevmCopyContextForLongJump(
    artdcevm::deoptimize_if_needed_abi::Context* context,
    uintptr_t* gprs,uintptr_t* fprs,uint32_t payload){
  if(!gprs||!fprs)std::abort();
  artdcevm::owned_context_consumer::CopyAndDestroy(
      artdcevm::deoptimize_if_needed_abi::OwnedContext(context),payload,
      *reinterpret_cast<uintptr_t (*)[34]>(gprs),
      *reinterpret_cast<uintptr_t (*)[32]>(fprs));
}

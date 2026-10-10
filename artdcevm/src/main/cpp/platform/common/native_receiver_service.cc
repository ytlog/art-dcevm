#include "platform/common/native_receiver_service.h"
#include <cstdlib>
namespace artdcevm::receiver_guard {
bool PermanentDispatch::Prepare(std::string*error){
 if(prepared_||consumer.load(std::memory_order_acquire)!=&guard_||!guard_.UsesFrames(frames_)||!frames_.Proven()||!guard_.BytecodeProven(frames_)){if(error)*error="Permanent receiver dispatch lacks the exact published guard/source FrameReader";return false;}
 auto proof=final_write_guard::FreezeReceiverProof(guard_);
 if(!final_write_guard::ValidateReceiverProof(proof)){if(error)*error="Permanent receiver dispatch lacks complete source listener/entry proofs";return false;}
 proof_=proof;prepared_=true;return true;
}
bool PermanentDispatch::Validate()const noexcept{
 validated_=prepared_&&consumer.load(std::memory_order_acquire)==&guard_&&guard_.UsesFrames(frames_)&&frames_.Proven()&&guard_.BytecodeProven(frames_)&&final_write_guard::ValidateReceiverProof(proof_);return validated_;
}
void PermanentDispatch::BeginCommit()const noexcept{
 if(!validated_||!prepared_||consumer.load(std::memory_order_acquire)!=&guard_)std::abort();
 final_write_guard::RetainInterpretationForReceiverCommit(proof_);
}
}

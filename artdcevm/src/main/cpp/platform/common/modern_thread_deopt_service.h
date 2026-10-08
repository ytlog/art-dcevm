#pragma once
#include "platform/abi/arm64/android16/thread_deoptimize.h"
#include "platform/common/owned_context_consumer.h"
#include "platform/common/thread_refresh.h"
#include <cstdlib>
#include <limits>
#include <utility>
namespace artdcevm::modern_provider {class OwnedThreadCalibration;}
namespace artdcevm::modern_thread_deopt_service {
namespace abi=deoptimize_if_needed_abi;
namespace consumer=owned_context_consumer;
// Capability owned by a trusted provider. Its issuer must establish exact ART
// build/profile identity, release ObjPtr representation, Context virtual ABI,
// reserve-marking build mode and fresh native owned-Thread oracle agreement.
// GC false/zero observations or matching source offsets alone are insufficient.
// No modern issuer/profile is implemented here. Tests define a mock issuer in
// their standalone executable; production cannot mint this from SDK or a bool.
using RequestEligibility=bool (*)(void* runtime,void* thread,uint64_t calibration_generation);
class CalibratedThreadToken {
 public:
  CalibratedThreadToken()=default;
 private:
  friend class artdcevm::modern_provider::OwnedThreadCalibration;
  friend class Service;
  CalibratedThreadToken(void* runtime,void* thread,thread_refresh::SourceDeclaration source,
                        consumer::MarkingRegister mode,uint64_t generation,RequestEligibility eligibility)
      :runtime_(runtime),thread_(thread),source_(source),generation_(generation),eligibility_(eligibility){
    thread_refresh::SourceLayout layout{};
    if(!thread_refresh::SourceLayoutFor(source_,&layout)||
       layout.gc_marking_offset>std::numeric_limits<uint16_t>::max()||
       layout.suspend_trigger_offset>std::numeric_limits<uint16_t>::max())return;
    refresh_={static_cast<uint16_t>(layout.gc_marking_offset),
              static_cast<uint16_t>(layout.suspend_trigger_offset),mode};
    valid_=runtime&&thread&&generation&&eligibility&&refresh_.Valid();
  }
  void* runtime_=nullptr;
  void* thread_=nullptr;
  thread_refresh::SourceDeclaration source_=thread_refresh::SourceDeclaration::kAndroid16;
  uint64_t generation_=0;
  consumer::ThreadRefreshDeclaration refresh_{};
  // Re-checks current owning Thread, mutator/call-site state and absence of a
  // pending Throwable; also rejects stale/revoked provider calibration. This
  // is request eligibility AFTER owned calibration, not a calibration bool.
  RequestEligibility eligibility_=nullptr;
  bool valid_=false;
};
class Service {
 public:
  // Transactional binding of both fixed named services. Failed lookup preserves
  // the previously authorized service, and never substitutes a void-return ABI.
  template<class Lookup>
  static bool Bind(const CalibratedThreadToken& token,void* runtime,Lookup lookup,Service* out){
    if(!out||!token.valid_||runtime!=token.runtime_)return false;
    Service candidate;
    candidate.push_=reinterpret_cast<thread_deoptimize_abi::PushContext>(lookup(thread_deoptimize_abi::kPushContext));
    if(!candidate.push_)return false;
    candidate.deopt_=reinterpret_cast<thread_deoptimize_abi::Deoptimize>(lookup(thread_deoptimize_abi::kDeoptimize));
    if(!candidate.deopt_)return false;
    candidate.token_=token;
    *out=candidate;return true;
  }
#if defined(__aarch64__)
  // Success never returns: native Thread conversion followed immediately by
  // calibrated Context consumption. Failure returns BEFORE any ART mutation.
  bool RunFull(void* runtime,void* thread,abi::JValue value,bool is_reference,
               abi::MethodType method_type)const{
    return Run(runtime,thread,value,is_reference,method_type,consumer::Jump);
  }
#endif
 private:
#ifdef ARTDCEVM_TESTING
  friend class ServiceTestPeer;
#endif
  using Consume=void (*)(abi::OwnedContext,const consumer::ThreadRefreshDeclaration&);
  bool Run(void* runtime,void* thread,abi::JValue value,bool is_reference,
           abi::MethodType method_type,Consume consume)const{
    if(!push_||!deopt_||!consume||!token_.valid_||runtime!=token_.runtime_||thread!=token_.thread_||
       (method_type!=abi::MethodType::kDefault&&method_type!=abi::MethodType::kKeepDexPc))return false;
    if(!token_.eligibility_(runtime,thread,token_.generation_))return false;
    // Full structural deopt begins with no pending Throwable (the existing
    // suspend eligibility protocol must ensure it). from_code=false keeps
    // runtime/suspend return semantics; method-exit callbacks have not run yet.
    push_(thread,value,is_reference,{0},false,method_type);
    abi::OwnedContext context=deopt_(thread,thread_deoptimize_abi::Kind::kFullFrame,false,false);
    if(!context)std::abort(); // The native stack may already have changed.
    consume(std::move(context),token_.refresh_);
    std::abort(); // A successful long jump cannot return to this native frame.
  }
  CalibratedThreadToken token_{};
  thread_deoptimize_abi::PushContext push_=nullptr;
  thread_deoptimize_abi::Deoptimize deopt_=nullptr;
};
}  // namespace artdcevm::modern_thread_deopt_service

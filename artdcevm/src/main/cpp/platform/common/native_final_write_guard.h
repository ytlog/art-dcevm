#pragma once
#include "platform/common/modern_final_field_guard.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>
namespace artdcevm::final_write_guard {
// All callbacks run with ART's shared mutator access. Identity readers must use
// calibrated fixed declarations/native root barriers; no public JNI is allowed.
struct Access {
 void* owner=nullptr;
 bool (*field)(void*,void*,uint32_t*,void**)=nullptr;
 bool (*method_owner)(void*,void*,void**)=nullptr;
 bool (*equivalent_owner)(void*,void*,void*)=nullptr;
 void (*raise)(void*,void*,const char*,const char*)=nullptr;
 // Native anchors survive suspension in weak-root readers. Prefer this path
 // when equivalence may suspend, then reread declaring roots after decoding.
 bool (*equivalent_writer)(void*,void* thread,void* field,void* method)=nullptr;
};
// reference comes from the independently decoded Field descriptor. A proof
// must observe the matching real listener overload, not another event slot.
struct Oracle {void* thread=nullptr;void* method=nullptr;void* field=nullptr;bool reference=false;};
// A listener's existence does not prove enforcement. The exact bytecode writer
// must reach the listener, receive IllegalAccessError, and leave its independent
// sentinel unchanged. The native event only proves callback/target identity;
// EndProof consumes the independently observed exception/value result.
class Guard {
 public:
 class ProofWitness final {public:ProofWitness()=default;private:friend class Guard;const Guard*owner_=nullptr;uint64_t epoch_=0;};
 ProofWitness FreezeProof()const noexcept;
 bool ValidateProof(const ProofWitness&)const noexcept;
 void CommitProof(const ProofWitness&)noexcept;
 explicit Guard(Access access):access_(access){}
 // Calibration is an isolated producer, not a process permission lease.
 bool BeginCalibration() noexcept;
 bool CompleteCalibration() noexcept;
 bool BeginProof(const Oracle&) noexcept;
 bool EndProof(bool illegal_access_error,bool unchanged_sentinel) noexcept;
 bool Proven() const noexcept;
 // Bootstrap owner only, before any permission consumer can be committed.
 void ResetUncommittedProof() noexcept;
 // Returns true iff the original field write must stop because an exception
 // was raised. ART DoFieldPutCommon checks its pending exception after event.
 bool FieldWritten(void* thread,void* method,void* field,bool reference=false) noexcept;
 private:
 Access access_;mutable std::mutex lock_;uint64_t epoch_=0;Oracle oracle_{};bool probing_=false,seen_=false,rejected_=false,proven_=false,calibrating_=false;void* calibration_field_=nullptr;std::atomic<uint64_t> proof_epoch_{0};std::atomic<bool> proof_ready_{false},proof_committed_{false};
};
} // namespace artdcevm::final_write_guard

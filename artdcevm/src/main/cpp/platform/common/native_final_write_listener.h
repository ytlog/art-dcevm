#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_final_write_guard.h"
#include "platform/common/native_receiver_guard.h"
namespace artdcevm::final_write_guard {
// Call during the first Native JNI load before plugin initialization. Older
// named EnableDeoptimization must be witnessed before DeoptimizeEverything,
// whose fixed 11/12 body CHECKs this state. No Instrumentation bool is guessed.
bool ObserveLegacyDeoptimizationStartup(HookEngine&,std::string*);
// Dispatcher and its identity/root owner must live for the process lifetime:
// successful evolution permanently requires the interpreter guard. Installation
// and commit forwarding require ART's exclusive mutator / suspend-all boundary.
bool BindListener(HookEngine&,void* independently_observed_instrumentation,Guard&,std::string*);
bool InstallListenerAtExclusiveBoundary(std::string*);
// Reuse this exact Instrumentation/listener owner. Installs only new event bits
// DexPcMoved+FieldRead; FieldWritten uses the already registered callback.
// Receiver rejection precedes final-field checks and forbids original access.
bool BindReceiverEvents(void* independently_observed_instrumentation,receiver_guard::Guard&,std::string*);
bool InstallReceiverEventsAtExclusiveBoundary(std::string*);
bool ReceiverListenerProven(const receiver_guard::Guard&)noexcept;
class ReceiverProofWitness final {
 public:ReceiverProofWitness()=default;
 private:
 friend ReceiverProofWitness FreezeReceiverProof(const receiver_guard::Guard&)noexcept;
 friend bool ValidateReceiverProof(const ReceiverProofWitness&)noexcept;
 friend void RetainInterpretationForReceiverCommit(const ReceiverProofWitness&)noexcept;
 const receiver_guard::Guard*receivers_=nullptr;const Guard*finals_=nullptr;void*instrumentation_=nullptr;uint64_t generation_=0;
 receiver_guard::Guard::ProofWitness receiver_proof_;Guard::ProofWitness final_proof_;
};
// Ordinary freeze only; final validation and activation acquire no C++ mutex.
ReceiverProofWitness FreezeReceiverProof(const receiver_guard::Guard&)noexcept;
bool ValidateReceiverProof(const ReceiverProofWitness&)noexcept;
void RetainInterpretationForReceiverCommit(const ReceiverProofWitness&)noexcept;
// Isolated, independently validated fixture method only; bootstrap must never
// globally deoptimize production methods merely to obtain a calibration event.
bool PrepareProofMethodAtExclusiveBoundary(void* fixture_method,std::string*);
bool ListenerProven() noexcept;
bool ListenerProven(const Guard& exact_owner) noexcept;
// Must be called before publishing each validated final-bit transition. No JNI,
// allocation, symbol lookup, or refundable failure at this commit boundary.
void RetainInterpretationAtCommit();
#ifdef ARTDCEVM_TESTING
void NotifyWrittenForTesting(bool reference,void* thread,void* method,void* field);
#endif
} // namespace artdcevm::final_write_guard

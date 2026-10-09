#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_final_write_guard.h"
namespace artdcevm::receiver_guard {class Guard;}
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

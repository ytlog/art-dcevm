#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_final_write_guard.h"
namespace artdcevm::final_write_guard {
// Dispatcher and its identity/root owner must live for the process lifetime:
// successful evolution permanently requires the interpreter guard. Installation
// and commit forwarding require ART's exclusive mutator / suspend-all boundary.
bool BindListener(HookEngine&,void* independently_observed_instrumentation,Guard&,std::string*);
bool InstallListenerAtExclusiveBoundary(std::string*);
bool ListenerProven() noexcept;
// Must be called before publishing each validated final-bit transition. No JNI,
// allocation, symbol lookup, or refundable failure at this commit boundary.
void RetainInterpretationAtCommit();
#ifdef ARTDCEVM_TESTING
void NotifyWrittenForTesting(bool reference,void* thread,void* method,void* field);
#endif
} // namespace artdcevm::final_write_guard

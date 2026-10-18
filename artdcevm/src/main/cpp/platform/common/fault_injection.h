#pragma once

// The implementation and all JNI exports live in the app's Debug test module.
// Disabled builds do not evaluate fault conditions or counter arguments.
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
#include "platform/abi/arm64/common/redefinition.h"
#include <chrono>
namespace artdcevm::fault_injection {
enum class Counter { kDiscardedClasses, kAllocationOrder, kAllocatedTypes, kAllocatedGroups,
                     kValidatedGroups };
bool Inject(int stage);
void InterfacePrepared(bool prepared);
bool RejectThreadProbe();
int ImtFallbackMode();
void ReferenceRead(bool marking);
void SnapshotFinished(std::chrono::steady_clock::time_point start, int targets, int loaded);
void CorruptHolder(const abi::arm64::RedefinitionDataIter& data);
int Increment(Counter counter);
void Set(Counter counter, int value);
}
#define ARTDCEVM_IMT_FALLBACK_MODE() (::artdcevm::fault_injection::ImtFallbackMode())
#define ARTDCEVM_FAULT(stage) (::artdcevm::fault_injection::Inject(stage))
#define ARTDCEVM_FAULT_IF(stage, condition) ((condition) && ARTDCEVM_FAULT(stage))
#define ARTDCEVM_REJECT_THREAD_PROBE() (::artdcevm::fault_injection::RejectThreadProbe())
#define ARTDCEVM_TEST(...) __VA_ARGS__
#else
#define ARTDCEVM_IMT_FALLBACK_MODE() 0
#define ARTDCEVM_FAULT(stage) false
#define ARTDCEVM_FAULT_IF(stage, condition) false
#define ARTDCEVM_REJECT_THREAD_PROBE() false
#define ARTDCEVM_TEST(...)
#endif

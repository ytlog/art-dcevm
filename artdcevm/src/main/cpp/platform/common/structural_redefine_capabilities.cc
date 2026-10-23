#include "platform/common/structural_redefine_internal.h"

#include "platform/common/imt_sealing.h"
#include "platform/common/interface_method_selection.h"
#include "platform/common/invoke_kind.h"
#include "platform/common/private_continuation.h"
#include "platform/common/interpreter_events.h"
#include "platform/common/narrowed_references.h"
#include "support/json.h"
#include "platform/common/constructor_history.h"

namespace artdcevm::structural_redefine {
using namespace detail;
std::string Capabilities() {
  Process* selected;
  {
    std::lock_guard<std::mutex> installation_lock(installation_mutex);
    selected = process.get();
  }
  Process empty;
  Process& state = selected ? *selected : empty;
  std::lock_guard<std::mutex> lock(state.report_mutex);
  const std::string supported = state.installed ? "true" : "false";
  const bool methods = state.installed && state.capabilities.retirement;
  const bool fields = methods && state.capabilities.field_retirement;
  const std::string retirement = methods ? "true" : "false";
  const std::string field_retirement = fields ? "true" : "false";
  const auto selection_reason = [methods](bool interface_target) {
    const char* reason = methods ? invoke_kind::SelectionUnavailable(interface_target)
                                 : "method evolution unavailable";
    return QuoteJsonMutf8(reason ? reason : "");
  };
  interface_redefinition::Backend* interfaces = state.interface_backend.load(std::memory_order_acquire);
  const auto interface_timing = interfaces ? interfaces->LastTiming() : interface_redefinition::Timing{};
  // Both capability fields describe the same probe snapshot, including disablement.
  const std::string interface_state = interfaces ? interfaces->RuntimeState() : state.interface_runtime;
  return "{\"installed\":" + supported + ",\"standardRedefineRouted\":" + supported +
      ",\"memberAddition\":" + supported + ",\"memberModifierChange\":" + supported +
      ",\"classModifierChange\":" + supported + ",\"interfaceAddition\":" + retirement + ",\"superclassWidening\":" +
          (methods && state.capabilities.superclass_change ? "true" : "false") +
      ",\"interfaceRemoval\":" + (methods && state.capabilities.subclass_watch && imt_sealing::Available() && narrowed_references::Available() ? "true" : "false") +
      ",\"interfaceDispatchSealing\":" + QuoteJsonMutf8(imt_sealing::State()) +
      ",\"interfaceMethodSelection\":" + (methods && interface_method_selection::Available() ? "true" : "false") +
      ",\"retransformRouted\":" + (state.installed && state.capabilities.retransform ? "true" : "false") +
      ",\"privateDirectSelection\":" + (methods && invoke_kind::DirectSelectionServices() ? "true" : "false") +
      ",\"privateSelfSelection\":" + (methods && invoke_kind::SelectionServices() ? "true" : "false") +
      ",\"privateSelfSelectionClass\":" + (methods && invoke_kind::SelectionServices(false) ? "true" : "false") +
      ",\"privateSelfSelectionInterface\":" + (methods && invoke_kind::SelectionServices(true) ? "true" : "false") +
      ",\"privateSelfSelectionClassReason\":" + selection_reason(false) +
      ",\"privateSelfSelectionInterfaceReason\":" + selection_reason(true) +
      ",\"privateSelfContinuationPrepared\":" + (methods && private_continuation::Prepared() ? "true" : "false") +
      ",\"privateSelfContinuationProven\":" + (methods && private_continuation::Proven() ? "true" : "false") +
      ",\"privateSelfContinuationActive\":" + (methods && private_continuation::Active() ? "true" : "false") +
      ",\"privateSelfContinuationKinds\":" + (methods && private_continuation::Proven() ? "[\"class\",\"interface\"]" : "[]") +
      ",\"privateSelfContinuationFailure\":" + QuoteJsonMutf8(private_continuation::Failure()) +
      ",\"privateSelfContinuationEventPolicy\":\"skip-move-result\"" +
      ",\"privateSelfContinuationDeviceValidated\":" + (private_continuation::DeviceValidated() ? "true" : "false") +
      ",\"privateSelfContinuationExceptionAbi\":" + QuoteJsonMutf8(interpreter_events::ExceptionAbi()) +
      ",\"privateSelfContinuationIdentityService\":" + QuoteJsonMutf8(invoke_kind::ContinuationIdentityService()) +
      ",\"privateSelfContinuationDirectAccessProofs\":" + std::to_string(invoke_kind::DirectAccessProofs()) +
      ",\"privateSelfContinuationDirectBoundary\":" + QuoteJsonMutf8(invoke_kind::DirectBoundary()) +
      ",\"methodDeletion\":" + retirement + ",\"methodSignatureChange\":" + retirement +
      ",\"fieldDeletion\":" + field_retirement + ",\"fieldTypeChange\":" + field_retirement +
      ",\"memberMapping\":" + QuoteJsonMutf8(class_layout::State()) +
      ",\"constructorHistoryCacheReason\":" + QuoteJsonMutf8(methods ? constructor_history::CacheReason() : "method evolution unavailable") +
      ",\"constructorHistoryCacheProofs\":" + std::to_string(methods ? constructor_history::CacheProofs() : 0) +
      ",\"jniRetirementGuards\":" +
          std::to_string(member_retirement::JniRetirementGuardHits()) +
      ",\"subclassLoadRetries\":" + std::to_string(state.subclass_retries.load()) +
      ",\"interfaceRedefinition\":" + (state.installed && interface_state.rfind("verified: ", 0) == 0 ? "true" : "false") +
      ",\"interfaceRuntime\":" + QuoteJsonMutf8(interface_state) +
      ",\"interfaceSnapshotTypes\":" + std::to_string(interface_timing.types) +
      ",\"interfaceSnapshotNanos\":" + std::to_string(interface_timing.snapshot_ns) +
      ",\"interfaceAllocationNanos\":" + std::to_string(interface_timing.allocation_ns) +
      ",\"memberDeletion\":" + field_retirement + ",\"memberTypeChange\":" + field_retirement +
      ",\"hierarchyChange\":false,"
      "\"fullDcevmComplete\":false,\"diagnostic\":" + QuoteJsonMutf8(state.diagnostic) +
      ",\"lastRejection\":" + QuoteJsonMutf8(state.last_rejection) + "}";
}
}  // namespace artdcevm::structural_redefine

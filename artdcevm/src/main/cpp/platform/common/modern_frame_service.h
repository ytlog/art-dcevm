#pragma once
#include "platform/abi/arm64/android11/instrument_thread_stack.h"
#include "platform/abi/arm64/android13/instrument_thread_stack.h"

namespace artdcevm::modern_frame {
enum class Model { kLegacyNoForceExitStack, kLegacyExitStack, kFrameDeoptFlags };

// Bind only after independently matching the loaded ART module and calibrating
// its frame model. SDK is not a Mainline/vendor ABI key. Android 11–12.1 lacks
// the native boolean argument. Android 13 and 14+ share a signature but differ
// in their operations on frames.
// Every operation requires ART's exclusive mutator lock and live, calibrated
// instrumentation/thread objects. The caller manages suspension and lifetimes.
class Service {
 public:
  static const char* SymbolName(Model model) {
    switch(model) {
      case Model::kLegacyNoForceExitStack: return abi::arm64::android11::kInstrumentThreadStack;
      case Model::kLegacyExitStack:
      case Model::kFrameDeoptFlags: return abi::arm64::android13::kInstrumentThreadStack;
    }
    return nullptr;
  }
  // symbol must be SymbolName(model) from the matched module. Failure preserves out.
  static bool Bind(Model model, void* symbol, Service* output) {
    if (!output || !symbol || !SymbolName(model)) return false;
    Service candidate;
    candidate.model_=model;
    if(model==Model::kLegacyNoForceExitStack)
      candidate.instrument_legacy_=reinterpret_cast<abi::arm64::android11::InstrumentThreadStack>(symbol);
    else candidate.instrument_=reinterpret_cast<abi::arm64::android13::InstrumentThreadStack>(symbol);
    *output=candidate;
    return true;
  }

  // Android 11–13 fixed source model: install/record legacy exit-stack frames.
  // This does not force their deoptimization; the owning provider still applies
  // its calibrated frame flags/force-deopt generation to selected frames.
  bool PrepareLegacyStack(void* instrumentation, void* thread) const {
    if(model_==Model::kLegacyNoForceExitStack) {
      if(!instrument_legacy_ || !instrumentation || !thread) return false;
      instrument_legacy_(instrumentation,thread);
      return true;
    }
    return Invoke(Model::kLegacyExitStack,instrumentation,thread,false);
  }

  // Android 14–17 fixed source model: ART updates interpreter notifications,
  // enables exit hooks, and marks supported quick frames CheckCallerForDeopt.
  // It does not install the removed Thread instrumentation-stack map.
  bool PrepareCheckOnly(void* instrumentation, void* thread) const {
    return Invoke(Model::kFrameDeoptFlags,instrumentation,thread,false);
  }

  // ART additionally marks supported quick frames ForceDeoptForRedefinition
  // and publishes Thread::SetDeoptCheckRequired(true). Delegating the whole
  // operation avoids guessing either flag slots or the Thread bool layout.
  // Caller must hold ART's exclusive mutator lock, keep both objects alive,
  // and establish the calibrated debuggable/recoverable-code conditions.
  // Frames without the native deopt flag are not made recoverable by this call.
  // true means the void native call ran, not that every frame was deoptimized.
  bool ForceThreadFrames(void* instrumentation, void* thread) const {
    return Invoke(Model::kFrameDeoptFlags,instrumentation,thread,true);
  }

 private:
  bool Invoke(Model expected, void* instrumentation, void* thread, bool force) const {
    if (!instrument_ || model_!=expected || !instrumentation || !thread) return false;
    instrument_(instrumentation,thread,force);
    return true;
  }
  Model model_=Model::kLegacyExitStack;
  abi::arm64::android11::InstrumentThreadStack instrument_legacy_=nullptr;
  abi::arm64::android13::InstrumentThreadStack instrument_=nullptr;
};
}

#pragma once
#include "platform/abi/arm64/android11/async_deopt.h"
#include "platform/abi/arm64/android14/async_deopt.h"

namespace artdcevm::async_deopt_service {
enum class Declaration { kPcOnly, kMethodAndPc };

// The caller selects an independently calibrated ELF declaration. SDK ranges
// describe the fixed AOSP source families; SDK is not a Mainline/vendor ABI key.
// This adapter only calls a named function. It supplies no Thread, stack or
// transaction calibration and does not enable a backend or an Inline Hook.
class Service {
 public:
  static const char* SymbolName(Declaration declaration) {
    switch (declaration) {
      case Declaration::kPcOnly: return abi::arm64::android11::kIsAsyncDeoptimizeable;
      case Declaration::kMethodAndPc: return abi::arm64::android14::kIsAsyncDeoptimizeable;
    }
    return nullptr;
  }

  // symbol must have been resolved by SymbolName(declaration), in the matched
  // loaded ART module. Failure leaves the previous service unchanged.
  static bool Bind(Declaration declaration, void* symbol, Service* output) {
    if (!output || !symbol || !SymbolName(declaration)) return false;
    Service candidate;
    switch (declaration) {
      case Declaration::kPcOnly:
        candidate.pc_only_ = reinterpret_cast<abi::arm64::android11::IsAsyncDeoptimizeable>(symbol);
        break;
      case Declaration::kMethodAndPc:
        candidate.method_and_pc_ = reinterpret_cast<abi::arm64::android14::IsAsyncDeoptimizeable>(symbol);
        break;
    }
    *output = candidate;
    return true;
  }

  // Call under ART's required mutator access with live runtime/method storage.
  // The modern declaration requires the actual frame method. No method is
  // reconstructed from a PC. Zero PCs are forwarded unchanged to ART.
  // Returns call validity; the ART boolean is written only after a valid call.
  bool Query(const void* runtime, void* method, uintptr_t pc, bool* output) const {
    if (!runtime || !output) return false;
    if (pc_only_) {
      *output = pc_only_(runtime, pc);
      return true;
    }
    if (method_and_pc_ && method) {
      *output = method_and_pc_(runtime, method, pc);
      return true;
    }
    return false;
  }

 private:
  abi::arm64::android11::IsAsyncDeoptimizeable pc_only_ = nullptr;
  abi::arm64::android14::IsAsyncDeoptimizeable method_and_pc_ = nullptr;
};
}

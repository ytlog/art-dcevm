#pragma once
#include "platform/abi/arm64/common/context.h"
#include <cstdint>
namespace artdcevm::owned_context_calibration {
enum class Failure {kNone,kInvalidRequest,kUnavailable,kAllocation,kResetModel,kRegisterWrite,kResetRestore};
struct Report {uint32_t completed=0;uint32_t checks=0;Failure failure=Failure::kNone;};
// Only native owned Context allocation/register storage is calibrated here.
// No Thread, compiled frame, mutator lock, long jump, stack tagging or provider
// capability token is touched. Successful checks do not establish the tail ABI.
class Service {
 public:
  static const char* SymbolName(){return abi::arm64::android_common::kCreateContext;}
  static bool Bind(void* named_create,Service* output);
  // Caller must resolve the named factory from the loaded ARM64 ART. All used
  // virtual methods are the same fixed 11–17 prefix; the live object is allocated
  // by ART and always destroyed through its native deleting destructor. SP/PC
  // are test values in independent owned storage, never applied to hardware.
  bool Run(uint32_t iterations,Report* output) const;
 private:
  abi::arm64::android_common::CreateContext create_=nullptr;
};
}

#include "platform/common/owned_context_consumer.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// This is a CPU/ownership test using OUR Context and Thread fixture. Running
// it on API 33 does not calibrate any Android 16/17 ART object or enable a Profile.
using namespace artdcevm::owned_context_consumer;
extern "C" {
alignas(16) uintptr_t captured_gprs[34];
alignas(16) uintptr_t captured_fprs[32];
uintptr_t original_sp;
void ContextTestRun(bool reserved);
void ContextTestLanding();
}
namespace {
struct ThreadFixture {
  uint64_t guard;
  uint32_t marking;
  uint32_t padding;
  uintptr_t suspend_trigger;
};
ThreadFixture thread;
alignas(16) unsigned char landing_stack[65536];
uintptr_t expected_gprs[34], expected_fprs[32], platform_register;
int copies, destroys;
bool reserved;
uintptr_t Pattern(size_t i) { return 0x1020304050607000ULL + i; }
class MockContext final : public abi::Context {
 public:
  ~MockContext() override { assert(copies == destroys + 1); ++destroys; }
  void Reset() override { std::abort(); }
  void FillCalleeSaves(uint8_t*, const abi::QuickMethodFrameInfo&) override { std::abort(); }
  void SetSP(uintptr_t) override { std::abort(); }
  void SetPC(uintptr_t) override { std::abort(); }
  void SetArg0(uintptr_t) override { std::abort(); }
  bool IsAccessibleGPR(uint32_t) override { std::abort(); }
  uintptr_t* GetGPRAddress(uint32_t) override { std::abort(); }
  uintptr_t GetGPR(uint32_t) override { std::abort(); }
  void SetGPR(uint32_t, uintptr_t) override { std::abort(); }
  bool IsAccessibleFPR(uint32_t) override { std::abort(); }
  uintptr_t GetFPR(uint32_t) override { std::abort(); }
  void SetFPR(uint32_t, uintptr_t) override { std::abort(); }
  void SmashCallerSaves() override { std::abort(); }
  void CopyContextTo(uintptr_t* g, uintptr_t* f) override {
    assert(copies == destroys);
    ++copies;
    std::memcpy(g, expected_gprs, sizeof(expected_gprs));
    std::memcpy(f, expected_fprs, sizeof(expected_fprs));
  }
};
}
extern "C" [[noreturn]] void ContextTestJump(bool reserve, uintptr_t saved_sp,
                                            uintptr_t saved_x18) {
  original_sp = saved_sp;
  platform_register = saved_x18;
  reserved = reserve;
  for (size_t i = 0; i < 34; ++i) expected_gprs[i] = Pattern(i);
  for (size_t i = 0; i < 32; ++i) expected_fprs[i] = Pattern(i + 100);
  expected_gprs[19] = reinterpret_cast<uintptr_t>(&thread);
  expected_gprs[31] = reinterpret_cast<uintptr_t>(landing_stack + sizeof(landing_stack));
  expected_gprs[33] = reinterpret_cast<uintptr_t>(&ContextTestLanding);
  const ThreadRefreshDeclaration declaration{
      offsetof(ThreadFixture, marking), offsetof(ThreadFixture, suspend_trigger),
      reserve ? MarkingRegister::kReserved : MarkingRegister::kNotReserved};
  Jump(abi::OwnedContext(new MockContext), declaration);
}
int main() {
  for (int iteration = 0; iteration < 1000; ++iteration) {
    thread = {0xcafebabe, (iteration % 3 == 0 ? 0U : iteration % 3 == 1 ? 1U : 0xfedcba98U),
              0, Pattern(iteration + 200)};
    ContextTestRun(iteration % 2);
    assert(copies == iteration + 1 && destroys == iteration + 1);
    for (size_t i = 0; i < 31; ++i) {
      if (i == 16 || i == 17) continue;  // ABI IP scratch registers.
      const uintptr_t expected = i == 18 ? platform_register :
          i == 20 && reserved ? thread.marking :
          i == 21 ? thread.suspend_trigger : expected_gprs[i];
      assert(captured_gprs[i] == expected);
    }
    assert(captured_gprs[31] == expected_gprs[31]);
    assert(captured_gprs[31] != original_sp);
    for (size_t i = 0; i < 32; ++i) assert(captured_fprs[i] == expected_fprs[i]);
    assert(thread.guard == 0xcafebabe);
  }
  std::puts("ARM64 hardware Context consumer: 1000 reserved/unreserved jumps passed; native virtual copy/destruction, GPR/FPR, SP/PC, x18 and Thread fixture refresh verified. No ART calibration.");
}

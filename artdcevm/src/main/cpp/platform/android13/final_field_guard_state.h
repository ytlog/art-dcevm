#pragma once

#include <atomic>

namespace artdcevm::android13 {

// Once a field gains final, cached setters must stay guarded for the process.
// A later rejected transaction or field change must not disable these guards.
class FinalFieldGuardState {
 public:
  void Mark() noexcept { enabled_.store(true, std::memory_order_release); }
  bool Enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }

 private:
  std::atomic<bool> enabled_{false};
};

void MarkFinalFieldGuard() noexcept;
bool FinalFieldGuardEnabled() noexcept;

}  // namespace artdcevm::android13

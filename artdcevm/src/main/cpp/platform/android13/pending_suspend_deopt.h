#pragma once

#include <atomic>

namespace artdcevm::android13 {

// Owns the one pending thread without depending on ART's Thread layout.
class PendingSuspendDeopt {
 public:
  bool Mark(void* thread);
  bool IsPending(void* thread) const;
  bool Cancel(void* thread);
  bool Consume(void* thread);
  void Reset();

 private:
  std::atomic<void*> thread_{nullptr};
};

}  // namespace artdcevm::android13

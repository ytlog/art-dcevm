#include "platform/android13/pending_suspend_deopt.h"

#include <atomic>
#include <cassert>
#include <thread>

using artdcevm::android13::PendingSuspendDeopt;

int main() {
  PendingSuspendDeopt pending;
  int first = 0;
  int second = 0;
  assert(!pending.Mark(nullptr));
  assert(pending.Mark(&first));
  assert(!pending.Mark(&second));
  assert(!pending.Consume(&second));
  assert(pending.IsPending(&first));
  assert(pending.Consume(&first));
  assert(!pending.Consume(&first));

  std::atomic<bool> start{false};
  bool first_marked = false;
  bool second_marked = false;
  std::thread a([&] {
    while (!start.load(std::memory_order_acquire)) {}
    first_marked = pending.Mark(&first);
  });
  std::thread b([&] {
    while (!start.load(std::memory_order_acquire)) {}
    second_marked = pending.Mark(&second);
  });
  start.store(true, std::memory_order_release);
  a.join();
  b.join();
  assert(first_marked != second_marked);
  assert(pending.Consume(first_marked ? static_cast<void*>(&first)
                                      : static_cast<void*>(&second)));
  assert(pending.Mark(&first));
  pending.Reset();
  assert(!pending.IsPending(&first));
  assert(pending.Mark(&second));
  assert(pending.Cancel(&second));
}

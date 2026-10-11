#pragma once
#include <cstddef>
#include "platform/abi/arm64/android16/is_marked_visitor.h"
namespace artdcevm::native_weak_sweep_observer {
using OriginalSweep = void (*)(void*, art::IsMarkedVisitor*);
using CurrentThread = void* (*)();
struct Record { art::mirror::Object* before; art::mirror::Object* after; };
class Sink {public:virtual ~Sink()=default;virtual bool Observe(const Record&)noexcept=0;};
enum class Status { MissingOriginal, WrongRuntime, WrongOwner, InvalidBuffer, NullVisitor, Observing, Completed };
// Diagnostic only. These borrowed addresses are neither roots nor a noescape proof.
struct Report {
  Status status = Status::MissingOriginal;
  size_t calls = 0;
  size_t stored = 0;
  bool overflow = false;
  bool owner_changed = false;
  bool sink_rejected = false;
};
class Observer final {
 public:
  Observer(void* runtime, void* owner, CurrentThread current, OriginalSweep original)
      : runtime_(runtime), owner_(owner), current_(current), original_(original) {}
  // Call ONLY from the original named Runtime::SweepSystemWeaks hook ingress.
  // The original GC/migration supplies its existing locks and visitor. This method
  // never initiates a sweep. Caller storage must remain exclusive for this stack
  // invocation, and its borrowed records must be consumed in that same episode.
  // Wrong runtime/owner is forwarded unchanged, without observing. No permission
  // or noescape capability is issued, including for a Completed report.
  void OnOriginalSweep(void* actual_runtime, art::IsMarkedVisitor* visitor,
                       Record* records, size_t capacity, Report& report, Sink* sink=nullptr) const;
 private:
  void* const runtime_;
  void* const owner_;
  CurrentThread const current_;
  OriginalSweep const original_;
};
}

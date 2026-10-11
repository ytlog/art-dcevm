#include "platform/common/native_weak_sweep_observer.h"
#include <cstdint>
namespace artdcevm::native_weak_sweep_observer {
namespace {
class ForwardingVisitor final : public art::IsMarkedVisitor {
 public:
  ForwardingVisitor(art::IsMarkedVisitor& original, CurrentThread current,
                    void* owner, Record* records, size_t capacity, Report& report, Sink* sink)
      : original_(original), current_(current), owner_(owner), records_(records),
        capacity_(capacity), report_(report), sink_(sink) {}
  art::mirror::Object* IsMarked(art::mirror::Object* object) override {
    const bool same_before = current_() == owner_;
    // Do not catch exceptions or alter the original visitor's mark/move result.
    auto* result = original_.IsMarked(object);
    const bool same_after = current_() == owner_;
    if (report_.calls != SIZE_MAX) ++report_.calls;
    else report_.overflow = true;
    if (!same_before || !same_after) report_.owner_changed = true;
    else if(sink_){if(!sink_->Observe({object,result}))report_.sink_rejected=true;}
    else if (report_.stored < capacity_) records_[report_.stored++] = {object, result};
    else report_.overflow = true;
    return result;
  }
 private:
  art::IsMarkedVisitor& original_;
  CurrentThread current_;
  void* owner_;
  Record* records_;
  size_t capacity_;
  Report& report_;
  Sink* sink_;
};
}
void Observer::OnOriginalSweep(void* actual_runtime, art::IsMarkedVisitor* visitor,
                              Record* records, size_t capacity, Report& report, Sink* sink) const {
  report = {};
  if (!original_) return;
  if (!runtime_ || actual_runtime != runtime_) report.status = Status::WrongRuntime;
  else if (!owner_ || !current_ || current_() != owner_) report.status = Status::WrongOwner;
  else if (capacity && !records) report.status = Status::InvalidBuffer;
  else if (!visitor) report.status = Status::NullVisitor;
  else {
    report.status = Status::Observing;
    ForwardingVisitor forwarding(*visitor, current_, owner_, records, capacity, report, sink);
    original_(actual_runtime, &forwarding);
    if (current_() != owner_) report.owner_changed = true;
    report.status = Status::Completed;
    return;
  }
  original_(actual_runtime, visitor);
}
}

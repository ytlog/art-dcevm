#include "platform/common/native_weak_sweep_observer.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
static size_t allocations = 0;
void* operator new(size_t size) {
  ++allocations;
  if (auto* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
using namespace artdcevm::native_weak_sweep_observer;
namespace {
int runtime, other_runtime, owner, foreign_owner;
void* current = &owner;
void* Current() { return current; }
art::IsMarkedVisitor* expected_unwrapped = nullptr;
int sweeps = 0;
bool throw_sweep = false;
art::mirror::Object* objects[4] = {
 reinterpret_cast<art::mirror::Object*>(0x1000),
 reinterpret_cast<art::mirror::Object*>(0x2000),
 reinterpret_cast<art::mirror::Object*>(0x3000), nullptr};
art::mirror::Object* outcomes[4];
void Sweep(void* actual, art::IsMarkedVisitor* visitor) {
  assert(actual == &runtime || actual == &other_runtime);
  ++sweeps;
  if (expected_unwrapped) assert(visitor == expected_unwrapped);
  if (throw_sweep) throw 41;
  if (!visitor) return;
  for (size_t i = 0; i < 4; ++i) outcomes[i] = visitor->IsMarked(objects[i]);
}
struct Visitor final : art::IsMarkedVisitor {
  size_t calls = 0;
  int throw_at = -1;
  int change_at = -1;
  art::mirror::Object* IsMarked(art::mirror::Object* object) override {
    const auto n = calls++;
    assert(object == objects[n]);
    if (int(n) == throw_at) throw 73;
    if (int(n) == change_at) current = &foreign_owner;
    if (n == 1) return nullptr;
    if (n == 2) return reinterpret_cast<art::mirror::Object*>(0x4000);
    return object;
  }
};
void Run() {
  Observer observer(&runtime, &owner, Current, Sweep);
  Record records[4]; Report report;
  Visitor normal;
  const auto allocations_before = allocations;
  observer.OnOriginalSweep(&runtime, &normal, records, 4, report);
  assert(allocations == allocations_before);
  assert(report.status == Status::Completed && report.calls == 4 && report.stored == 4);
  assert(!report.overflow && !report.owner_changed);
  for (size_t i = 0; i < 4; ++i) {
    assert(records[i].before == objects[i]); assert(records[i].after == outcomes[i]);
  }
  assert(outcomes[0] == objects[0] && !outcomes[1]);
  assert(outcomes[2] == reinterpret_cast<art::mirror::Object*>(0x4000) && !outcomes[3]);
  Visitor limited;
  observer.OnOriginalSweep(&runtime, &limited, records, 1, report);
  assert(report.calls == 4 && report.stored == 1 && report.overflow && limited.calls == 4);
  Visitor zero;
  observer.OnOriginalSweep(&runtime, &zero, nullptr, 0, report);
  assert(report.calls == 4 && report.stored == 0 && report.overflow);
  Visitor invalid;
  expected_unwrapped = &invalid;
  observer.OnOriginalSweep(&runtime, &invalid, nullptr, 1, report);
  assert(report.status == Status::InvalidBuffer && report.calls == 0 && invalid.calls == 4);
  Visitor wrong_runtime;
  expected_unwrapped = &wrong_runtime;
  observer.OnOriginalSweep(&other_runtime, &wrong_runtime, records, 4, report);
  assert(report.status == Status::WrongRuntime && wrong_runtime.calls == 4);
  current = &foreign_owner;
  Visitor wrong_owner;
  expected_unwrapped = &wrong_owner;
  observer.OnOriginalSweep(&runtime, &wrong_owner, records, 4, report);
  assert(report.status == Status::WrongOwner && wrong_owner.calls == 4);
  current = &owner; expected_unwrapped = nullptr;
  observer.OnOriginalSweep(&runtime, nullptr, records, 4, report);
  assert(report.status == Status::NullVisitor);
  const auto before = sweeps;
  Observer missing(&runtime, &owner, Current, nullptr);
  missing.OnOriginalSweep(&runtime, &normal, records, 4, report);
  assert(report.status == Status::MissingOriginal && sweeps == before);
  Visitor drift; drift.change_at = 1;
  observer.OnOriginalSweep(&runtime, &drift, records, 4, report);
  assert(report.status == Status::Completed && report.owner_changed && report.stored == 1);
  assert(report.calls == 4 && drift.calls == 4 && outcomes[2] == reinterpret_cast<art::mirror::Object*>(0x4000));
  current = &owner;
  Visitor throws; throws.throw_at = 2;
  try { observer.OnOriginalSweep(&runtime, &throws, records, 4, report); assert(false); }
  catch (int code) { assert(code == 73); }
  assert(report.status == Status::Observing && report.calls == 2 && report.stored == 2);
  throw_sweep = true;
  Visitor no_calls;
  try { observer.OnOriginalSweep(&runtime, &no_calls, records, 4, report); assert(false); }
  catch (int code) { assert(code == 41); }
  assert(report.status == Status::Observing && report.calls == 0 && no_calls.calls == 0);
  throw_sweep = false;
}
}
int main() { Run(); std::puts("PASS: exact forwarding, bounds, owner/runtime refusals, original exceptions"); }

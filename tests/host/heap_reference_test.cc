#include "platform/android13/heap_reference.h"
#include "platform/abi/arm64/android13/heap_reference_abi.h"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace heap = artdcevm::android13;
namespace abi = heap::heap_reference_abi;
namespace {
struct Holder { abi::ObjectPrefix object; uint32_t reference; };
constexpr size_t offset = offsetof(Holder, reference);
constexpr uint32_t old_ref = 0x1000, relocated_ref = 0x2000, writer_ref = 0x3000;
constexpr uint32_t gray = abi::kGrayState << abi::kReadBarrierShift;
enum class Mode { kRelocate, kPublication, kRacingWrite };
Mode mode = Mode::kRelocate;
std::atomic<size_t> marks{0};
std::atomic<int> stage{0};
int* published = nullptr;

void* Mark(void* target) {
  marks.fetch_add(1, std::memory_order_relaxed);
  if (target == nullptr) return nullptr;
  assert(reinterpret_cast<uintptr_t>(target) == old_ref);
  if (mode == Mode::kPublication) assert(*published == 42);
  if (mode == Mode::kRacingWrite) {
    stage.store(1, std::memory_order_release);
    while (stage.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  }
  return reinterpret_cast<void*>(relocated_ref);
}
class Hooks : public artdcevm::HookEngine {
 public:
  bool available = true;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* name) override {
    assert(std::strcmp(library, "libart.so") == 0);
    assert(std::strcmp(name, "artReadBarrierMark") == 0);
    return available ? reinterpret_cast<void*>(Mark) : nullptr;
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
void Store(uint32_t* field, uint32_t value, int ordering = __ATOMIC_RELAXED) {
  __atomic_store_n(field, value, ordering);
}
void Publication(bool is_volatile) {
  for (int iteration = 0; iteration < 128; ++iteration) {
    Holder holder{};
    int value = 0;
    published = &value;
    std::thread reader([&] {
      while (heap::ReadHeapReference(&holder, offset, is_volatile) !=
             reinterpret_cast<void*>(relocated_ref)) std::this_thread::yield();
      assert(value == 42);
    });
    std::thread writer([&] {
      value = 42;
      Store(&holder.reference, old_ref);
      Store(&holder.object.monitor, gray, __ATOMIC_RELEASE);
    });
    reader.join(); writer.join();
    assert(__atomic_load_n(&holder.reference, __ATOMIC_RELAXED) == old_ref);
  }
}
void RacingWrite(bool is_volatile) {
  for (int iteration = 0; iteration < 128; ++iteration) {
    Holder holder{{0, gray}, old_ref};
    stage.store(0, std::memory_order_relaxed);
    std::thread reader([&] {
      assert(heap::ReadHeapReference(&holder, offset, is_volatile) ==
             reinterpret_cast<void*>(relocated_ref));
    });
    std::thread writer([&] {
      while (stage.load(std::memory_order_acquire) != 1) std::this_thread::yield();
      Store(&holder.reference, writer_ref, is_volatile ? __ATOMIC_SEQ_CST : __ATOMIC_RELAXED);
      stage.store(2, std::memory_order_release);
    });
    reader.join(); writer.join();
    // Ordinary ART getters don't CAS the marked old value over a concurrent store.
    assert(__atomic_load_n(&holder.reference, __ATOMIC_RELAXED) == writer_ref);
  }
}
void VolatilePublication() {
  for (int iteration = 0; iteration < 128; ++iteration) {
    Holder holder{};
    int value = 0;
    std::thread reader([&] {
      while (heap::ReadHeapReference(&holder, offset, true) == nullptr) std::this_thread::yield();
      assert(value == 42);
    });
    std::thread writer([&] { value = 42; Store(&holder.reference, writer_ref, __ATOMIC_SEQ_CST); });
    reader.join(); writer.join();
  }
}
}
int main() {
  Hooks hooks;
  std::string error;
  hooks.available = false;
  assert(!heap::ConfigureHeapReferenceReader(hooks, &error) && !error.empty());
  hooks.available = true;
  assert(heap::ConfigureHeapReferenceReader(hooks, &error));
  for (bool is_volatile : {false, true}) {
    Holder holder{{0, 0xe000ffff}, old_ref};
    const auto before = marks.load();
    assert(heap::ReadHeapReference(&holder, offset, is_volatile) == reinterpret_cast<void*>(old_ref));
    assert(marks.load() == before);  // Lock/hash/mark bits do not make the owner gray.
    Store(&holder.reference, 0);
    assert(heap::ReadHeapReference(&holder, offset, is_volatile) == nullptr);
    assert(marks.load() == before);
    Store(&holder.object.monitor, gray | 0xe000ffff);
    assert(heap::ReadHeapReference(&holder, offset, is_volatile) == nullptr);
    assert(marks.load() == before + 1);  // Gray null goes through ART's marker.
    Store(&holder.reference, old_ref);
    assert(heap::ReadHeapReference(&holder, offset, is_volatile) == reinterpret_cast<void*>(relocated_ref));
    assert(__atomic_load_n(&holder.reference, __ATOMIC_RELAXED) == old_ref);
  }
  mode = Mode::kPublication;
  Publication(false); Publication(true);
  mode = Mode::kRacingWrite;
  RacingWrite(false); RacingWrite(true);
  mode = Mode::kRelocate;
  VolatilePublication();
  assert(heap::HeapReferenceMarkCalls() == marks.load());
  std::cout << "Baker heap-reference reader: gray/null/lock bits, acquire publication, volatile publication, relocation and racing writes passed\n";
}

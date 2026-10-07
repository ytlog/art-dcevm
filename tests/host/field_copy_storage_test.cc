#include "platform/android13/field_copy_storage.h"
#include "platform/android13/field_copy_storage_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference_abi.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

namespace copy = artdcevm::android13;
namespace abi = copy::field_copy_storage_abi;
namespace {
struct alignas(1024) Object { unsigned char bytes[64]; };
alignas(8) thread_local unsigned char thread_storage[abi::kThreadCardTableOffset + sizeof(uintptr_t)];
uint8_t card = 0;
std::atomic<size_t> markers{0};
void* Current() { return thread_storage; }
void* Mark(void* target) {
  markers.fetch_add(1, std::memory_order_relaxed);
  return target ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) + 0x1000) : nullptr;
}
void SetCardOwner(Object& object) {
  const uintptr_t biased = reinterpret_cast<uintptr_t>(&card) -
                           (reinterpret_cast<uintptr_t>(&object) >> abi::kCardShift);
  std::memcpy(thread_storage + abi::kThreadCardTableOffset, &biased, sizeof(biased));
}
class Hooks : public artdcevm::HookEngine {
 public:
  bool thread_available = true, marker_available = true;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* symbol) override {
    assert(std::strcmp(library, "libart.so") == 0);
    if (std::strcmp(symbol, abi::kCurrentThread) == 0)
      return thread_available ? reinterpret_cast<void*>(Current) : nullptr;
    assert(std::strcmp(symbol, copy::heap_reference_abi::kReadBarrierMark) == 0);
    return marker_available ? reinterpret_cast<void*>(Mark) : nullptr;
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
copy::field_copy_abi::ArtField Field(bool is_volatile, uint32_t offset = 16) {
  return {0, is_volatile ? abi::kVolatileFlag : 0, 0, offset};
}
void Monitor(Object& object, uint32_t value) {
  __atomic_store_n(reinterpret_cast<uint32_t*>(object.bytes + abi::kMonitorOffset), value,
                   __ATOMIC_RELAXED);
}
uint32_t Monitor(Object& object) {
  return __atomic_load_n(reinterpret_cast<uint32_t*>(object.bytes + abi::kMonitorOffset),
                         __ATOMIC_RELAXED);
}
void PrimitiveTypes(bool is_volatile) {
  Object object;
  auto field = Field(is_volatile);
  const char types[] = {'Z', 'B', 'C', 'S', 'I', 'F', 'J', 'D'};
  const unsigned widths[] = {1, 1, 2, 2, 4, 4, 8, 8};
  // Float/double patterns include -0, infinities, NaNs and their payload bits.
  const uint64_t patterns[] = {0, 1, 0x80, 0xff, 0x8000, 0xffff, 0x80000000,
      0x7f800000, 0x7fc01234, 0x7fa01234, 0xffffffff, 0x8000000000000000ULL,
      0x7ff0000000000000ULL, 0x7ff8123456789abcULL, 0xffffffffffffffffULL};
  for (size_t type = 0; type < sizeof(types); ++type) {
    for (uint64_t pattern : patterns) {
      std::memset(object.bytes, 0xa5, sizeof(object.bytes));
      copy::WriteCopyValue(&object, &field, types[type], pattern);
      const uint64_t mask = widths[type] == 8 ? UINT64_MAX :
          (uint64_t{1} << (widths[type] * 8)) - 1;
      assert(copy::ReadCopyValue(&object, &field, types[type]) == (pattern & mask));
      for (size_t index = 0; index < sizeof(object.bytes); ++index) {
        if (index < field.offset || index >= field.offset + widths[type])
          assert(object.bytes[index] == 0xa5);
      }
    }
  }
}
void References(bool is_volatile) {
  Object object{};
  auto field = Field(is_volatile);
  SetCardOwner(object);
  __atomic_store_n(&card, 0, __ATOMIC_RELAXED);
  copy::WriteCopyValue(&object, &field, 'L', 0);
  assert(copy::ReadCopyValue(&object, &field, 'L') == 0);
  assert(__atomic_load_n(&card, __ATOMIC_RELAXED) == 0);
  copy::WriteCopyValue(&object, &field, '[', 0x124000);
  assert(copy::ReadCopyValue(&object, &field, '[') == 0x124000);
  assert(__atomic_load_n(&card, __ATOMIC_RELAXED) == abi::kCardDirty);
  const size_t before = markers.load();
  Monitor(object, copy::heap_reference_abi::kGrayState << copy::heap_reference_abi::kReadBarrierShift);
  assert(copy::ReadCopyValue(&object, &field, 'L') == 0x125000);
  assert(markers.load() == before + 1);
  copy::WriteCopyValue(&object, &field, 'L', 0);
  assert(copy::ReadCopyValue(&object, &field, 'L') == 0);
  assert(markers.load() == before + 2);
  // Clearing references does not clear a card that is already dirty.
  assert(__atomic_load_n(&card, __ATOMIC_RELAXED) == abi::kCardDirty);
}
void Monitors() {
  Object source{}, target{};
  const uint32_t values[] = {0, 0x00010002, 0x80012345, 0xffffffff};
  for (uint32_t value : values) {
    std::memset(source.bytes, 0xa5, sizeof(source.bytes));
    std::memset(target.bytes, 0x5a, sizeof(target.bytes));
    Monitor(source, value);
    copy::TransferCopyMonitor(&target, &source);
    assert(Monitor(source) == abi::kDefaultLockWord && Monitor(target) == value);
    for (size_t index = 0; index < sizeof(source.bytes); ++index) {
      if (index < abi::kMonitorOffset || index >= abi::kMonitorOffset + sizeof(uint32_t)) {
        assert(source.bytes[index] == 0xa5 && target.bytes[index] == 0x5a);
      }
    }
  }
}
void ConcurrentJavaData(bool is_volatile) {
  Object object{};
  auto field = Field(is_volatile);
  std::atomic<bool> done{false};
  const uint64_t first = 0x5555555555555555ULL, second = 0xaaaaaaaaaaaaaaaaULL;
  copy::WriteCopyValue(&object, &field, 'J', first);
  std::thread writer([&] {
    for (unsigned index = 0; index < 100000; ++index)
      copy::WriteCopyValue(&object, &field, 'J', index % 2 ? first : second);
    done.store(true, std::memory_order_release);
  });
  do {
    const auto value = copy::ReadCopyValue(&object, &field, 'J');
    assert(value == first || value == second);
  } while (!done.load(std::memory_order_acquire));
  writer.join();
}
void VolatilePublication() {
  Object object{};
  auto flag = Field(true);
  int payload = 0;
  std::thread writer([&] {
    payload = 37;
    copy::WriteCopyValue(&object, &flag, 'I', 1);
  });
  while (copy::ReadCopyValue(&object, &flag, 'I') != 1) std::this_thread::yield();
  assert(payload == 37);
  writer.join();
}
void ConcurrentCardMarks() {
  Object object{};
  __atomic_store_n(&card, 0, __ATOMIC_RELAXED);
  std::thread first([&] { SetCardOwner(object); for (int i = 0; i < 10000; ++i) copy::CopyWriteBarrier(&object); });
  std::thread second([&] { SetCardOwner(object); for (int i = 0; i < 10000; ++i) copy::CopyWriteBarrier(&object); });
  first.join(); second.join();
  assert(__atomic_load_n(&card, __ATOMIC_RELAXED) == abi::kCardDirty);
}
}
int main() {
  Hooks hooks;
  std::string error;
  hooks.thread_available = false;
  assert(!copy::ConfigureFieldCopyStorage(hooks, &error));
  hooks.thread_available = true; hooks.marker_available = false;
  assert(!copy::ConfigureFieldCopyStorage(hooks, &error));
  hooks.marker_available = true;
  assert(copy::ConfigureFieldCopyStorage(hooks, &error));
  PrimitiveTypes(false); PrimitiveTypes(true);
  References(false); References(true);
  Monitors(); ConcurrentJavaData(false); ConcurrentJavaData(true);
  VolatilePublication(); ConcurrentCardMarks();
  std::puts("field-copy storage: primitive bits, width boundaries, reference barriers, monitor, concurrent atomics passed");
}

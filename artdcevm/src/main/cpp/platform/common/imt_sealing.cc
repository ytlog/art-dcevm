#include "platform/common/imt_sealing.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/class_layout_table.h"
#include "platform/symbols/imt_sealing_symbols.h"

namespace artdcevm::imt_sealing {
namespace {
namespace abi = artdcevm::abi::arm64;
using class_layout::Fail;
using runtime_thread::VerifiedThread;

// runtime/imtable.h, Android 11--17: ImTable::kSize ArtMethod pointers.
constexpr size_t kImtSize = 43;
// runtime/mirror/object.h, Android 11--17: Object::kVTableLength. Every class
// vtable starts with java.lang.Object's virtual methods.
constexpr int32_t kObjectVTableLength = 11;
// runtime/linear_alloc.h, ART 14--17: LinearAllocKind::kNoGCRoots. An ImTable
// holds native ArtMethod pointers only.
constexpr uint32_t kNoGCRoots = 0;

using CreateConflictMethod = void* (*)(void* runtime, void* linear_alloc);
using Alloc = void* (*)(void* linear_alloc, void* thread, size_t size);
using AllocKind = void* (*)(void* linear_alloc, void* thread, size_t size, uint32_t kind);

struct Services {
  CreateConflictMethod create_conflict_method = nullptr;
  Alloc alloc = nullptr;
  AllocKind alloc_kind = nullptr;
  abi::ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
};
std::mutex state_mutex;
Services services;
std::atomic<bool> ready{false};
std::string state = "unavailable: not prepared";

void* AllocTable(void* linear_alloc, void* thread) {
  constexpr size_t kBytes = kImtSize * sizeof(void*);
  return services.alloc_kind ? services.alloc_kind(linear_alloc, thread, kBytes, kNoGCRoots)
                             : services.alloc(linear_alloc, thread, kBytes);
}

// ClassLinker::GetAllocatorForClassLoader reads ClassLoader.allocator, the
// LinearAlloc ART created with the loader's class table and allocates the
// loader's classes, methods and IMTs in.
void* LoaderAllocator(const VerifiedThread& thread, uint32_t loader_reference, std::string* why) {
  auto* loader = class_layout::Heap<abi::Object>(thread, loader_reference);
  if (!loader) return Fail(why, "a boot class cannot be sealed"), nullptr;
  void* loader_class = class_layout::Heap<void>(thread, loader->klass);
  abi::ArtField* field = loader_class ? services.find_instance_field(loader_class, "allocator", "J") : nullptr;
  if (!field || (field->access_flags & evolution::kAccStatic))
    return Fail(why, "ClassLoader.allocator is not an instance field"), nullptr;
  const auto allocator = *reinterpret_cast<const uint64_t*>(reinterpret_cast<const uint8_t*>(loader) + field->offset);
  if (!allocator) return Fail(why, "the class loader has no LinearAlloc"), nullptr;
  return class_layout::Pointer<void>(allocator);
}

struct SealOps {
  bool (*build)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, void*, void***, std::string*);
  void (*publish)(void*, void**);
};

template <typename Class, typename Method> struct Sealer {
  // mirror/class.h, Android 11--17: EmbeddedVTableLengthOffset is
  // sizeof(Class); ImtPtrOffset rounds the int32 after it up to the pointer
  // size; the embedded vtable follows the IMT pointer.
  static constexpr size_t kVTableLengthOffset = ObjectSize(static_cast<const Class*>(nullptr));
  static constexpr size_t kImtOffset = (kVTableLengthOffset + sizeof(int32_t) + 7) & ~size_t{7};
  static constexpr size_t kVTableOffset = kImtOffset + sizeof(void*);

  static uint8_t* Bytes(Class* klass) { return reinterpret_cast<uint8_t*>(klass); }
  // Checks the embedded-table offsets against what ART linked: the vtable
  // fits the class size, and each declared or copied virtual method sits at
  // its own method index.
  static bool Probe(Class* klass, std::string* why) {
    const int32_t length = *reinterpret_cast<const int32_t*>(Bytes(klass) + kVTableLengthOffset);
    if (length < kObjectVTableLength ||
        kVTableOffset + static_cast<size_t>(length) * sizeof(void*) > klass->class_size)
      return Fail(why, "embedded vtable length does not fit the class size");
    auto* const* vtable = reinterpret_cast<Method* const*>(Bytes(klass) + kVTableOffset);
    auto* methods = class_layout::Members<Method>(klass->methods);
    constexpr uint32_t kDirect = evolution::kAccStatic | evolution::kAccPrivate | evolution::kAccConstructor;
    for (uint32_t i = 0; methods && i < methods->length; ++i) {
      Method* method = methods->At(i);
      if (method->access_flags.load(std::memory_order_relaxed) & kDirect) continue;
      if (method->method_index >= length || vtable[method->method_index] != method)
        return Fail(why, "embedded vtable does not hold a declared virtual method at its index");
    }
    auto* const* imt = *reinterpret_cast<void** const*>(Bytes(klass) + kImtOffset);
    if (!imt) return Fail(why, "class has no IMT");
    for (size_t i = 0; i < kImtSize; ++i)
      if (!imt[i]) return Fail(why, "IMT has an empty slot");
    return true;
  }

  // A new IMT for `object` whose every slot is its own fresh conflict method:
  // ART fills a non-shared conflict method's table in place, and a subclass
  // may copy each slot separately. Null `table` for an abstract class.
  static bool Build(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self, void* object,
                    void*** table, std::string* why) {
    auto* klass = static_cast<Class*>(object);
    *table = nullptr;
    if (klass->access_flags & (evolution::kAccInterface | evolution::kAccAbstract)) return true;
    if (!Probe(klass, why)) return false;
    void* linear_alloc = LoaderAllocator(thread, klass->class_loader, why);
    if (!linear_alloc) return false;
    auto** slots = static_cast<void**>(AllocTable(linear_alloc, self->driver->self));
    if (!slots) return Fail(why, "cannot allocate an IMT");
    for (size_t slot = 0; slot < kImtSize; ++slot) {
      slots[slot] = services.create_conflict_method(self->driver->runtime, linear_alloc);
      if (!slots[slot]) return Fail(why, "cannot allocate an IMT conflict method");
    }
    *table = slots;
    return true;
  }
  static void Publish(void* klass, void** table) {
    __atomic_store_n(reinterpret_cast<void***>(Bytes(static_cast<Class*>(klass)) + kImtOffset), table,
                     __ATOMIC_RELEASE);
  }
  static constexpr SealOps Make(int) { return {Build, Publish}; }
};

// Indexed like class_layout's probes.
const auto kSealers = class_layout::Table<Sealer, SealOps>();
}  // namespace

bool Prepare(const class_layout::Services& layout, std::string* failure) {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (ready.load()) return true;
  Services found;
  found.create_conflict_method =
      reinterpret_cast<CreateConflictMethod>(symbols::Find(symbols::imt_sealing::kCreateImtConflictMethod));
  found.alloc = reinterpret_cast<Alloc>(symbols::Find(symbols::imt_sealing::kLinearAllocAlloc));
  found.alloc_kind = reinterpret_cast<AllocKind>(symbols::Find(symbols::imt_sealing::kLinearAllocAllocKind));
  found.find_instance_field = layout.find_instance_field;
  std::string reason;
  if (!found.create_conflict_method) reason = std::string("missing ") + symbols::imt_sealing::kCreateImtConflictMethod.name;
  else if (!found.alloc == !found.alloc_kind) reason = "expected exactly one LinearAlloc::Alloc";
  else if (!found.find_instance_field) reason = "no instance field lookup";
  if (!reason.empty()) {
    state = "unavailable: " + reason;
    *failure = reason;
    return false;
  }
  services = found;
  state = "available";
  ready.store(true);
  return true;
}

bool Available() { return ready.load(); }

std::string State() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return state;
}

bool Build(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self, void* klass, int selected,
           void*** table, std::string* why) {
  if (!Available()) return Fail(why, State());
  if (selected < 0 || static_cast<size_t>(selected) >= kSealers.size()) return Fail(why, "no verified class layout");
  return kSealers[selected].build(thread, self, klass, table, why);
}

void Publish(void* klass, int selected, void** table) { kSealers[selected].publish(klass, table); }

bool Seal(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
          const abi::RedefinitionDataIter& data, int selected, std::string* why) {
  auto* classes = data.Classes(false);
  if (!classes) return true;  // An interface group's other entries carry no arrays.
  for (int32_t i = 0; i < classes->length; ++i) {
    void* klass = class_layout::Element<void>(thread, classes, i);
    if (!klass) return Fail(why, "missing replacement class");
    void** table = nullptr;
    if (!Build(thread, self, klass, selected, &table, why)) return false;
    // The replacement is not published: no other thread reads its IMT yet.
    if (table) Publish(klass, selected, table);
  }
  return true;
}
}  // namespace artdcevm::imt_sealing

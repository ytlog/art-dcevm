#include "platform/common/imt_sealing.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/fault_injection.h"
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

using GetConflictMethod = void* (*)(void* runtime);
using Alloc = void* (*)(void* linear_alloc, void* thread, size_t size);
using AllocKind = void* (*)(void* linear_alloc, void* thread, size_t size, uint32_t kind);

struct Services {
  GetConflictMethod conflict_method = nullptr;
  Alloc alloc = nullptr;
  AllocKind alloc_kind = nullptr;
  abi::ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
};
std::mutex state_mutex;
Services services;
std::atomic<bool> ready{false}, installed{false};
std::atomic<void*> canonical_conflict{nullptr};
std::atomic<bool> protecting{false};
std::atomic<const char*> captured_from{nullptr};
using Link = bool (*)(void*, void*, abi::Handle, abi::Handle, bool*, void**);
void* original_link_methods = nullptr;
void* original_fill_super = nullptr;
struct Boundary { void* target = nullptr; const char* name = nullptr; bool legacy = false; };
Boundary boundary;
std::atomic<void*> verified_boundary{nullptr};
thread_local bool priming = false;
thread_local uint32_t probe_links = 0, probe_super_fills = 0;

void Capture(void* conflict, const char* source) noexcept {
  if (!conflict) return;
  void* empty = nullptr;
  if (canonical_conflict.compare_exchange_strong(empty, conflict, std::memory_order_release))
    captured_from.store(source, std::memory_order_release);
}
void Protect(bool* new_conflict, void** imt) noexcept {
  if (!protecting.load(std::memory_order_acquire)) return;
  if (void* conflict = canonical_conflict.load(std::memory_order_acquire)) {
    std::fill_n(imt, kImtSize, conflict);
    *new_conflict = true;
  }
}
// Fixed Android 11/12 declaration. LinkInterfaceMethods invokes this for
// every concrete class with a superclass before any per-method IMT inserts.
// SetIMTRef recognizes the canonical runtime method and cannot turn these
// slots back into direct entries; ART still owns the final table publication.
void FillSuper(void* self, abi::Handle klass, void* unimplemented, void* conflict,
               bool* new_conflict, void** imt) noexcept {
  if (priming) ++probe_super_fills;
  Capture(conflict, "FillImtFromSuperClass");
  using Fill = void (*)(void*, abi::Handle, void*, void*, bool*, void**);
  reinterpret_cast<Fill>(original_fill_super)(self, klass, unimplemented, conflict, new_conflict, imt);
  Protect(new_conflict, imt);
}
void* Canonical(void* runtime) {
  if (services.conflict_method) {
    void* result = services.conflict_method(runtime);
    Capture(result, "Runtime::GetImtConflictMethod");
  }
  return canonical_conflict.load(std::memory_order_acquire);
}

// Both ClassLinker::LinkMethods (11--17) and the 13--17 k64 helper have
// these explicit arguments. No ART object or thread field is read here.
// The output remains on LinkClass's stack until ART allocates its own IMT,
// installs it, then publishes kResolved. Setting new_conflict prevents ART
// from sharing an ancestor's IMT/cache even when every slot initially matches.
bool LinkMethods(void* self, void* thread, abi::Handle klass, abi::Handle interfaces,
                 bool* new_conflict, void** imt) noexcept {
  if (priming) ++probe_links;
  const bool had_conflict = *new_conflict;
  if (!reinterpret_cast<Link>(original_link_methods)(self, thread, klass, interfaces, new_conflict, imt))
    return false;
  // Fixed ART 13--17 LinkClass initializes new_conflict=false. The modern
  // LinkMethods helper only copies the superclass IMT or writes through
  // SetIMTRef (FinalizeIfTable and FillIMTFromIfTable with create_tables=false).
  // Each false->true transition writes the canonical conflict method into a
  // slot. Further SetIMTRef writes to that runtime slot stay canonical. Thus
  // true at return guarantees at least one canonical slot remains. If all 43
  // nonnull outputs are identical, that sole value is necessarily canonical.
  // This runs only in Prime's thread window, before our own Protect modifies
  // outputs. No Class/Runtime layout or arbitrary published IMT is inspected.
  if (priming && !had_conflict && *new_conflict && imt[0] &&
      std::all_of(imt + 1, imt + kImtSize, [first = imt[0]](void* slot) { return slot == first; }))
    Capture(imt[0], "LinkMethods saturated probe");
  Protect(new_conflict, imt);
  return true;
}
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

  // A private IMT of canonical conflict slots. ART recognizes the canonical
  // method and lazily creates a private method/cache for each actually used
  // slot, instead of allocating 43 empty conflict methods in every transaction.
  // Abstract classes have no IMT; LinkMethods protects their later descendants.
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
    void* conflict = Canonical(self->driver->runtime);
    if (!conflict) return Fail(why, "ART has no canonical IMT conflict method");
    std::fill_n(slots, kImtSize, conflict);
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

bool Prepare(const class_layout::Services& layout, std::vector<Hook>* hooks, std::string* failure) {
  std::lock_guard<std::mutex> lock(state_mutex);
  ready.store(false);
  Services found;
  const int fault_mode = ARTDCEVM_IMT_FALLBACK_MODE();
  found.conflict_method = (fault_mode & 1) ? nullptr :
      reinterpret_cast<GetConflictMethod>(symbols::Find(symbols::imt_sealing::kGetImtConflictMethod));
  found.alloc = reinterpret_cast<Alloc>(symbols::Find(symbols::imt_sealing::kLinearAllocAlloc));
  found.alloc_kind = reinterpret_cast<AllocKind>(symbols::Find(symbols::imt_sealing::kLinearAllocAllocKind));
  found.find_instance_field = layout.find_instance_field;
  std::string reason;
  if (!found.alloc == !found.alloc_kind) reason = "expected exactly one LinearAlloc::Alloc";
  else if (!found.find_instance_field) reason = "no instance field lookup";
  const auto* link_symbol = &symbols::imt_sealing::kLinkMethods64;
  void* link = (fault_mode & 2) ? nullptr : symbols::Find(*link_symbol);
  // A legacy LinkMethods wrapper can also have a name while its callers
  // inline it. The distinctive legacy FillSuper is the explicit-argument
  // pre-publication boundary on those runtimes.
  void* fill_super = link ? nullptr : symbols::Find(symbols::imt_sealing::kFillImtFromSuperClass);
  if (!link && !fill_super) {
    link_symbol = &symbols::imt_sealing::kLinkMethods;
    link = (fault_mode & 2) ? nullptr : symbols::Find(*link_symbol);
  }
  if (reason.empty() && !link && !fill_super) reason = "missing pre-publication LinkMethods or FillImtFromSuperClass service";
  if (!reason.empty()) {
    state = "unavailable: " + reason;
    *failure = reason;
    return false;
  }
  // Always append on retry: all earlier tokens may have been rolled back.
  if (link) {
    hooks->push_back({link, HookAddress(LinkMethods), &original_link_methods, link_symbol->name});
    boundary = {link, link_symbol == &symbols::imt_sealing::kLinkMethods64 ? "LinkMethodsHelper" : "LinkMethods", false};
  } else {
    hooks->push_back({fill_super, HookAddress(FillSuper), &original_fill_super,
                      symbols::imt_sealing::kFillImtFromSuperClass.name});
    boundary = {fill_super, "FillImtFromSuperClass", true};
  }
  services = found;
  state = "available";
  ready.store(true);
  return true;
}

void Installed(bool value) { installed.store(value, std::memory_order_release); }
bool Available() {
  return ready.load() && installed.load(std::memory_order_acquire) &&
      (verified_boundary.load(std::memory_order_acquire) == boundary.target) &&
      (services.conflict_method || canonical_conflict.load(std::memory_order_acquire));
}
bool Prime(JNIEnv* jni, std::string* why) {
  if (!ready.load() || !installed.load()) return Fail(why, "linking protection is not installed");
  if ((verified_boundary.load(std::memory_order_acquire) == boundary.target) &&
      (services.conflict_method || canonical_conflict.load(std::memory_order_acquire))) return true;
  probe_links = 0;
  probe_super_fills = 0;
  priming = true;
  jclass probe = jni ? jni->FindClass("com/github/ytlog/artdcevm/internal/ImtLinkProbe") : nullptr;
  priming = false;
  if (probe) jni->DeleteLocalRef(probe);
  const bool exception = jni && jni->ExceptionCheck();
  if (exception) jni->ExceptionClear();
  const bool reached = boundary.legacy ? probe_super_fills != 0
                                        : probe_links != 0;
  if (!exception && probe && reached && (services.conflict_method || canonical_conflict.load(std::memory_order_acquire))) {
    verified_boundary.store(boundary.target, std::memory_order_release);
    return true;
  }
  std::lock_guard<std::mutex> lock(state_mutex);
  state = exception ? "unavailable: Java exception linking canonical conflict probe"
      : !probe ? "unavailable: canonical conflict probe class was not found"
      : !reached ? "unavailable: selected pre-publication probe callback was not reached"
      : "unavailable: canonical conflict probe was not saturated; LinkMethods calls=" +
          std::to_string(probe_links);
  ready.store(false);
  return Fail(why, state);
}
bool Enable(abi::ClassRedefinitionPrefix* self, std::string* why) {
  if (!Available()) return Fail(why, State());
  void* conflict = Canonical(self->driver->runtime);
  if (!conflict) return Fail(why, "ART has no canonical IMT conflict method");
  protecting.store(true, std::memory_order_release);
  return true;
}
bool Protecting() { return protecting.load(std::memory_order_acquire); }

std::string State() {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (ready.load() && !installed.load()) return "unavailable: pre-publication linking protection is not installed";
  if (ready.load() && installed.load()) {
    const char* source = services.conflict_method ? "Runtime::GetImtConflictMethod" : captured_from.load(std::memory_order_acquire);
    return std::string("available: ") + boundary.name + "; canonical=" + (source ? source : "awaiting capture");
  }
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

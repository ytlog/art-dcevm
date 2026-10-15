#include "platform/common/interface_redefinition.h"

#include <android/log.h>
#include <android/api-level.h>
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <limits>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <unordered_map>
#include <string_view>
#include "platform/abi/arm64/android13/interface_runtime.h"
#include "platform/abi/arm64/android16/interface_runtime.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/common/class_linker.h"
#include "platform/abi/arm64/common/root_visitor.h"
#include "platform/abi/arm64/android13/redefinition.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/art_hook.h"
#include "platform/common/interface_snapshot.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::interface_redefinition {
namespace {
using namespace abi::arm64;
thread_local const InterfaceRuntime* runtime = nullptr;
template <class T> T& Field(void* object, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
using Admission = jvmtiError (*)(Handle, std::string*);
using Mirror = void* (*)(ClassRedefinitionPrefix*);
using Ensure = bool (*)(void*, void*, Handle, bool, bool);
using LookupReferrer = void* (*)(void*, TypeIndex, void*);
using LookupCache = void* (*)(void*, TypeIndex, void*, void*);
using LookupChars = void* (*)(void*, void*, const char*, void*);
using LookupView = void* (*)(void*, void*, std::string_view, void*);
using CacheAlloc = void* (*)(ClassRedefinitionPrefix*, Handle);
using ArrayAlloc = ObjectArray* (*)(void*, void*, int32_t);
using SetCache = void (*)(void*, void*);
using Allocate = void* (*)(ClassRedefinitionPrefix*, Handle, Handle, Handle, uint16_t);
LookupChars lookup_chars = nullptr;
LookupView lookup_view = nullptr;
CacheAlloc allocate_cache = nullptr;
ArrayAlloc allocate_array = nullptr;
SetCache set_cache = nullptr;
Allocate allocate_class = nullptr;
void* (*mark)(void*) = nullptr;
void* original_lookup_referrer = nullptr;
void* original_lookup_cache = nullptr;
void* (*current_thread)() = nullptr;
Mirror mirror_class = nullptr;
ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
Ensure ensure_initialized = nullptr;
jobject (*new_local_ref)(JNIEnv*, void*) = nullptr;
void (*delete_local_ref)(JNIEnv*, jobject) = nullptr;
std::atomic<void*> class_linker{nullptr};
void* original_normal[2]{};
void* original_structural[2]{};
void* original_finish = nullptr;
void* original_run = nullptr;
void* original_link_class = nullptr;
void (*set_status_internal)(void*, android13::ClassStatus) = nullptr;
bool available = false;
std::atomic<const char*> disabled_reason{nullptr};
std::atomic<int> verified_release{0};
void (*visit_handle_roots)(void*, RootVisitor*, uint32_t) = nullptr;
const InterfaceRuntime* RuntimeFor(int release) {
  if (release == 13) return &android13::kInterfaceRuntime;
  if (release == 16) return &android16::kInterfaceRuntime;
  return nullptr;
}
void Disable(const char* reason) noexcept {
  disabled_reason.store(reason);
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", reason);
}

struct Target { jclass klass; bool boot; jint status; TypeSnapshot snapshot; evolution::ClassSchema live; jclass owner = nullptr; };
struct State { State* previous; std::vector<Target> targets; JNIEnv* jni; jvmtiEnv* env; bool thread_verified; jclass array_class; jint batch_size; const InterfaceRuntime* previous_runtime; bool runtime_verified = false; int selected_release = 0; const Target* rejected_target = nullptr; const char* rejection = nullptr; const jvmtiClassDefinition* definitions = nullptr; std::vector<jclass> shared_ancestors{}; const std::string* rejection_class = nullptr; };
thread_local State* state = nullptr;
thread_local Timing timing;
struct JniFailure {};
class LocalRef {
 public:
  explicit LocalRef(void* object) : reference_(new_local_ref(state->jni, object)) {}
  ~LocalRef() { delete_local_ref(state->jni, reference_); }
  jobject Get() const { return reference_; }
 private:
  jobject reference_;
};
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
thread_local int failure_stage = 0;
thread_local int discarded_classes = 0;
thread_local int failure_hits = 0;
thread_local int allocation_order = 0;
thread_local int allocated_types = 0;
thread_local int allocated_groups = 0;
bool Inject(int stage) {
  if (failure_stage != stage) return false;
  failure_stage = 0;
  ++failure_hits;
  return true;
}
#endif

void* Decode(jclass klass) {
  RedefinerPrefix driver{nullptr, 0, nullptr, current_thread()};
  ClassRedefinitionPrefix lookup{&driver, klass, nullptr};
  return mirror_class(&lookup);
}
const Target* Find(void* klass) {
  if (!state) return nullptr;
  for (const auto& target : state->targets)
    if (Decode(target.klass) == klass) return &target;
  return nullptr;
}
ClassStatus Status(void* klass) {
  // mirror/class.h: status occupies the four most significant bits.
  return static_cast<ClassStatus>(__atomic_load_n(&Field<uint32_t>(klass, runtime->status),
                                                __ATOMIC_ACQUIRE) >> kClassStatusShift);
}
jvmtiError Reject(std::string* message, jvmtiError error, const char* why) {
  state->rejection = why;  // Static reason; formatting waits until Native return.
  *message = why;
  return error;
}
bool VerifyThread(const InterfaceRuntime& layout, void* thread, JNIEnv* jni) {
  return thread && Field<void*>(thread, layout.jni_env) == jni &&
      Field<uint32_t>(thread, layout.tid) == static_cast<uint32_t>(syscall(__NR_gettid)) &&
      Field<uint32_t>(thread, layout.is_gc_marking) <= 1;
}
// Observe ART's existing scope chain. No candidate root is installed, no object
// is dereferenced, and the callback only records addresses into fixed storage.
class ScopeRoots final : public RootVisitor {
 public:
  static constexpr size_t kMaxRoots = 64;
  static constexpr size_t kMaxHeader = sizeof(android16::HandleScopeHeader);
  explicit ScopeRoots(void* scope, size_t capacity)
      : begin(reinterpret_cast<uintptr_t>(scope)), end(begin + kMaxHeader + 4 * capacity) {}
  void VisitRoots(void***, size_t, const RootInfo&) noexcept override { unexpected = true; }
  void VisitRoots(uint32_t** roots, size_t count, const RootInfo&) noexcept override {
    for (size_t i = 0; i < count; ++i) {
      const uintptr_t address = reinterpret_cast<uintptr_t>(roots[i]);
      if (address >= begin && address < end && (address - begin) % 4 == 0)
        observed[(address - begin) / 4] = true;
    }
  }
  bool Matches(const InterfaceRuntime& layout, void* scope, const uint32_t* target,
               uint32_t size) const {
    const auto* start = static_cast<uint8_t*>(scope) + layout.scope_header_size;
    const uintptr_t address = reinterpret_cast<uintptr_t>(target);
    if (unexpected || address < reinterpret_cast<uintptr_t>(start) ||
        address >= reinterpret_cast<uintptr_t>(start + 4 * size)) return false;
    for (size_t offset = 0; begin + offset < end; offset += 4) {
      const bool expected = offset >= layout.scope_header_size &&
          offset < layout.scope_header_size + 4 * size &&
          Field<uint32_t>(scope, offset) != 0;
      if (observed[offset / 4] != expected) return false;
    }
    return observed[(address - begin) / 4];
  }
 private:
  uintptr_t begin, end;
  std::array<bool, kMaxRoots + kMaxHeader / 4> observed{};
  bool unexpected = false;
};
bool VerifyScopes(const InterfaceRuntime& layout, void* thread, Handle target) {
  void* scope = Field<void*>(thread, layout.top_handle_scope);
  // Both fixed sources create a StackHandleScope<1> immediately around the
  // admission call. Check its known root address BEFORE reading the candidate
  // header: a mismatched Thread slot must never be followed as a pointer.
  if (!scope || reinterpret_cast<uintptr_t>(scope) + layout.scope_header_size !=
                    reinterpret_cast<uintptr_t>(target.reference)) return false;
  const int32_t capacity = Field<int32_t>(scope, layout.scope_capacity);
  const uint32_t size = layout.scope_size == InterfaceRuntime::kAbsent
      ? capacity : Field<uint32_t>(scope, layout.scope_size);
  if (capacity != 1 || size != 1) return false;
  ScopeRoots observed(scope, capacity);
  visit_handle_roots(thread, &observed, Field<uint32_t>(thread, layout.tid));
  return observed.Matches(layout, scope, target.reference, size);
}

// Only a failed independent declaration probe disables the process. A target
// that cannot supply the required member evidence rejects this transaction.
const char* SelectRuntime(Handle klass, const Target& target) {
  if (!state->thread_verified) {
    Disable("interface Thread declaration did not match JNI environment/tid");
    return disabled_reason.load();
  }
  RedefinerPrefix driver{nullptr, 0, nullptr, current_thread()};
  ClassRedefinitionPrefix context{&driver, target.klass, nullptr};
  if (!method_retirement::VerifyTarget(&context, target.live)) {
    return "interface target is unsupported by Class member probes";
  }
  bool supported_candidate = false;
  for (int release : method_retirement::CandidateReleases(true)) {
    const InterfaceRuntime* candidate = RuntimeFor(release);
    if (!candidate) continue;
    supported_candidate = true;
    if (!VerifyThread(*candidate, driver.self, state->jni)) continue;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    if (Inject(13)) { Disable("interface Thread probe rejected injected mismatch"); return disabled_reason.load(); }
#endif
    if (!VerifyScopes(*candidate, driver.self, klass)) continue;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    if (Inject(14)) { Disable("interface HandleScope probe rejected injected mismatch"); return disabled_reason.load(); }
#endif
    runtime = candidate;
    state->runtime_verified = true;
    state->selected_release = release;
    return nullptr;
  }
  if (!supported_candidate) return "interface target has no supported Class runtime candidate";
  Disable("interface HandleScope/Thread declarations did not match verified Class candidates");
  return disabled_reason.load();
}
void* Read(uint32_t reference);
enum class PointerMarker { kClear, kMarked, kUnavailable };
bool NamedReference(void* object, std::string_view name, std::string_view descriptor, void** value) {
  void* type = Read(static_cast<Object*>(object)->klass);
  const auto* field = find_instance_field(type, name, descriptor);
  if (!field) return false;
  *value = Read(__atomic_load_n(&Field<uint32_t>(object, field->offset), __ATOMIC_ACQUIRE));
  return true;
}
PointerMarker CheckPointerMarker(void* klass) {
  // ClassExt::Has*PointerIdMarker in both fixed sources: nonnull and not array.
  // Every managed reference offset comes from ART's named field table.
  void* ext = nullptr;
  if (!NamedReference(klass, "extData", "Ldalvik/system/ClassExt;", &ext))
    return PointerMarker::kUnavailable;
  if (!ext) return PointerMarker::kClear;
  for (const auto name : {"instanceJfieldIDs", "jmethodIDs", "staticJfieldIDs"}) {
    void* ids = nullptr;
    if (!NamedReference(ext, name, "Ljava/lang/Object;", &ids)) return PointerMarker::kUnavailable;
    if (!ids) continue;
    void* component = nullptr;
    if (!NamedReference(Read(static_cast<Object*>(ids)->klass), "componentType", "Ljava/lang/Class;", &component))
      return PointerMarker::kUnavailable;
    if (!component) return PointerMarker::kMarked;
  }
  return PointerMarker::kClear;
}
jvmtiError Admit(Handle klass, std::string* message, bool structural, size_t spelling) noexcept try {
  void* original = structural ? original_structural[spelling] : original_normal[spelling];
  void* object = klass.Get<void>();
  const Target* target = Find(object);
  if (!target) return reinterpret_cast<Admission>(original)(klass, message);
  state->rejected_target = target;
  state->rejection_class = nullptr;
  if (!Available()) return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, disabled_reason.load());
  if (const char* reason = SelectRuntime(klass, *target))
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, reason);
  if ((Status(object) >= ClassStatus::kInitialized) !=
      ((target->status & JVMTI_CLASS_STATUS_INITIALIZED) != 0))
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, "interface initialization state changed since snapshot");
  if (target->boot)
    return Reject(message, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface redefinition rejects boot interfaces");
  void* linker = class_linker.load();
  if (!linker)
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, "interface redefinition ClassLinker unavailable");
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(8)) throw std::bad_alloc{};
#endif
  if (structural) {
    const Target* owner = Find(Decode(target->owner));
    if (!owner) return Reject(message, JVMTI_ERROR_INTERNAL, "interface pointer-id snapshot owner unavailable");
    for (size_t i = 0; i < owner->snapshot.types.size(); ++i) {
      PointerMarker marker = CheckPointerMarker(Decode(owner->snapshot.types[i]));
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
      // Test the semantic result, never write a marker into a published ClassExt.
      if ((i == 0 && Inject(19)) || (i > 0 && Inject(20))) marker = PointerMarker::kMarked;
#endif
      if (marker == PointerMarker::kClear) continue;
      state->rejection_class = &owner->snapshot.descriptors[i];
      return Reject(message, marker == PointerMarker::kMarked ? JVMTI_ERROR_UNMODIFIABLE_CLASS
                                                             : JVMTI_ERROR_NOT_AVAILABLE,
                    marker == PointerMarker::kMarked ? "active pointer JNI IDs prevent structural redefinition"
                                                      : "interface pointer-id field table is unavailable");
    }
    void* thread = current_thread();
    if (Status(object) == ClassStatus::kInitializing && Field<uint32_t>(object, runtime->clinit_thread_id) == Field<uint32_t>(thread, runtime->tid))
      return Reject(message, JVMTI_ERROR_INTERNAL, "interface redefinition rejects redefinition during initialization");
    if (!ensure_initialized(linker, thread, klass, true, true)) {
      // Thread::ClearException() in runtime/thread.h is this assignment.
      Field<void*>(thread, runtime->exception) = nullptr;
      return Reject(message, JVMTI_ERROR_INVALID_CLASS, "interface redefinition target failed initialization");
    }
  }
  return JVMTI_ERROR_NONE;
} catch (...) {
  // Even writing the diagnostic string can allocate. Do not write one here.
  if (runtime) Field<void*>(current_thread(), runtime->exception) = nullptr;
  return ArtHookExceptionError();
}
jvmtiError NormalCan(Handle klass, std::string* message) noexcept { return Admit(klass, message, false, 0); }
jvmtiError NormalGet(Handle klass, std::string* message) noexcept { return Admit(klass, message, false, 1); }
jvmtiError StructuralCan(Handle klass, std::string* message) noexcept { return Admit(klass, message, true, 0); }
jvmtiError StructuralGet(Handle klass, std::string* message) noexcept { return Admit(klass, message, true, 1); }
// Match HandleScope::Create/Thread::PushHandleScope rather than borrowing
// unrooted raw pointers across ART allocations. No Java allocation occurs while
// gathering the Class objects or constructing this C++ storage.
class Roots {
 public:
  Roots(void* thread, size_t count) : thread_(thread), storage_(new uint8_t[runtime->scope_header_size + 4 * count]{}) {
    Field<void*>(storage_.get(), runtime->scope_link) = Field<void*>(thread_, runtime->top_handle_scope);
    Field<int32_t>(storage_.get(), runtime->scope_capacity) = static_cast<int32_t>(count);
    Field<void*>(thread_, runtime->top_handle_scope) = storage_.get();
  }
  ~Roots() { Field<void*>(thread_, runtime->top_handle_scope) = Field<void*>(storage_.get(), runtime->scope_link); }
  Roots(const Roots&) = delete;
  Roots& operator=(const Roots&) = delete;
  Handle Add(void* object) {
    auto* slot = reinterpret_cast<uint32_t*>(storage_.get() + runtime->scope_header_size) + used_++;
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    if (runtime->scope_size != InterfaceRuntime::kAbsent)
      Field<uint32_t>(storage_.get(), runtime->scope_size) = static_cast<uint32_t>(used_);
    return {slot};
  }
 private:
  void* thread_;
  std::unique_ptr<uint8_t[]> storage_;
  size_t used_ = 0;
};
void Assign(Handle handle, void* object) {
  *const_cast<uint32_t*>(handle.reference) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
}
void* Read(uint32_t reference) {
  void* object = reinterpret_cast<void*>(static_cast<uintptr_t>(reference));
  void* thread = current_thread();
  // Source read barriers run only while this mutator is marking. Mark performs
  // the ART collector operation; this module does not implement a collector.
  return object && Field<uint32_t>(thread, runtime->is_gc_marking) ? mark(object) : object;
}
void Store(ObjectArray* array, int32_t index, void* value) {
  // The only JNI operation permitted inside our ART allocation hook: the
  // current thread writes transaction-private arrays/holder, outside callbacks
  // and ART locks. ART 13/16 ScopedThreadStateChange is a no-op for Runnable ->
  // Runnable. SetObjectArrayElement performs ART's store and GC write barrier,
  // with no managed allocation or suspension point. The named local-reference
  // helpers only bridge these rooted objects; capacity is reserved while Native.
  // Never use this helper from LinkClass, a class visitor, or a locked region.
  LocalRef destination(array), element(value);
  state->jni->SetObjectArrayElement(static_cast<jobjectArray>(destination.Get()), index, element.Get());
  if (state->jni->ExceptionCheck()) throw JniFailure{};
}
void* Slot(const RedefinitionDataIter& data, int32_t slot) {
  return Read(data.holder->array.Get<ObjectArray>()->elements[data.index * RedefinitionDataIter::kNumSlots + slot]);
}
void SetSlot(const RedefinitionDataIter& data, int32_t slot, void* object) {
  Store(data.holder->array.Get<ObjectArray>(), data.index * RedefinitionDataIter::kNumSlots + slot, object);
}
// Run's caller owns the DexFiles. This root outlives Run's holder and keeps
// every linked replacement reachable until the result is known, including
// partially completed allocations that never reach the holder's new_classes.
struct Attempt { RedefinerPrefix* driver; std::vector<Handle> replacements; Handle pending; bool committed = false; jvmtiError error = JVMTI_ERROR_NONE; };
thread_local Attempt* attempt = nullptr;
Handle ReplacementRoot(const Target& target) {
  return attempt->replacements[&target - state->targets.data()];
}
void DiscardClass(void* klass) {
  if (!klass) return;
  const ClassStatus status = Status(klass);
  if (status >= ClassStatus::kResolved ||
      status == ClassStatus::kErrorResolved) {
    // Unpublished classes have no copied statics or initializer to preserve.
    // kErrorResolved would still enter the next heap subtype scan.
    set_status_internal(klass, android13::ClassStatus::kErrorUnresolved);
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    ++discarded_classes;
#endif
  }
}
void Discard(const Attempt& next) {
  if (next.committed) return;
  // A linked class can outlive a failed allocator without reaching its array.
  DiscardClass(next.pending.Get<void>());
  for (Handle root : next.replacements) {
    auto* classes = root.Get<ObjectArray>();
    for (int32_t i = 0; classes && i < classes->length; ++i)
      DiscardClass(Read(classes->elements[i]));
  }
}

class AttemptScope {
 public:
  explicit AttemptScope(Attempt* next) : previous_(std::exchange(attempt, next)) {}
  ~AttemptScope() { Discard(*attempt); attempt = previous_; }
 private:
  Attempt* previous_;
};
jvmtiError Run(RedefinerPrefix* driver) noexcept {
  using Function = jvmtiError (*)(RedefinerPrefix*);
  if (!state || state->targets.empty())
    return reinterpret_cast<Function>(original_run)(driver);
  if (!state->thread_verified || !state->runtime_verified) {
    driver->result = JVMTI_ERROR_NOT_AVAILABLE;
    state->rejection = "interface transaction entered Run without completed runtime probes";
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  if (driver->self != current_thread()) {
    Disable("interface Redefiner Thread declaration mismatch");
    state->rejection = disabled_reason.load();
    driver->result = JVMTI_ERROR_NOT_AVAILABLE;
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  try {
    Roots roots(driver->self, state->targets.size() + 1);
    Attempt next{driver, {}, roots.Add(nullptr)};
    for (size_t i = 0; i < state->targets.size(); ++i) next.replacements.push_back(roots.Add(nullptr));
    AttemptScope scope(&next);
    const jvmtiError result = reinterpret_cast<Function>(original_run)(driver);
    next.committed = result == JVMTI_ERROR_NONE;
    return result;
  } catch (const std::bad_alloc&) {
    // Only this hook's root allocation may throw here; deeper hooks catch
    // at their own boundary, before returning through ART.
    driver->result = JVMTI_ERROR_OUT_OF_MEMORY;
    return JVMTI_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    Field<void*>(driver->self, runtime->exception) = nullptr;
    driver->result = ArtHookExceptionError();
    return static_cast<jvmtiError>(driver->result);
  }
}
struct Linking {
  const std::vector<Handle>* old_types;
  Handle new_types;
  Handle private_cache;
  int32_t index;
};
thread_local const Linking* linking = nullptr;
bool LinkClass(void* linker, void* thread, const char* descriptor, Handle klass,
               Handle interfaces, Handle* output) noexcept {
  using Function = bool (*)(void*, void*, const char*, Handle, Handle, Handle*);
  const bool result = reinterpret_cast<Function>(original_link_class)(
      linker, thread, descriptor, klass, interfaces, output);
  // Capture the resolved class before AllocateNewClassObject's JNI-ID array
  // allocations. Those can fail after linking and return nullptr to Finish.
  // This callback may run with ClassLinker locks held. Only assign an existing
  // HandleScope root: no JNI/local-table growth, heap store or allocation here.
  if (linking && !descriptor && output && output->Get<void>())
    Assign(attempt->pending, output->Get<void>());
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  // A link failure after producing the class exercises the allocator's null
  // return path, before Finish has recorded this replacement in the holder.
  if (result && linking && linking->index == 1 && Inject(1)) return false;
#endif
  return result;
}
class LinkingScope {
 public:
  explicit LinkingScope(const Linking* scope) : previous_(std::exchange(linking, scope)) {}
  ~LinkingScope() { linking = previous_; }
 private:
  const Linking* previous_;
};
void* Redirect(void* linker, TypeIndex index, void* cache, void* loader) noexcept try {
  if (!linking || cache != linking->private_cache.Get<void>()) return nullptr;
  const auto* dex = reinterpret_cast<const DexFilePrefix*>(static_cast<DexCache*>(cache)->dex_file);
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(11)) throw std::bad_alloc{};
#endif
  std::string type, error;
  if (!dex || !evolution::ReadTypeDescriptor(DexViewOf(*dex), index.index, &type, &error)) return nullptr;
  // Unlike DoLookupResolvedType, descriptor lookup does not write the cache.
  // A redirected entry must remain empty, so later inline cache probes cannot
  // find an old interface and bypass this hook during the same link.
  void* old = lookup_view ? lookup_view(linker, current_thread(), type, loader)
                         : lookup_chars(linker, current_thread(), type.c_str(), loader);
  for (size_t i = 0; i < linking->old_types->size(); ++i)
    if ((*linking->old_types)[i].Get<void>() == old)
      return Read(linking->new_types.Get<ObjectArray>()->elements[i]);
  return nullptr;
} catch (...) {
  // Lookup cannot return a JVMTI error. Retain it until Finish regains control;
  // the result of this link will never be committed.
  attempt->error = ArtHookExceptionError();
  return nullptr;
}
void* LookupByCache(void* linker, TypeIndex index, void* cache, void* loader) noexcept {
  if (void* object = Redirect(linker, index, cache, loader)) return object;
  return reinterpret_cast<LookupCache>(original_lookup_cache)(linker, index, cache, loader);
}
void* LookupByReferrer(void* linker, TypeIndex index, void* referrer) noexcept {
  if (linking) {
    if (void* object = Redirect(linker, index, Read(Field<uint32_t>(referrer, runtime->dex_cache)),
                               Read(Field<uint32_t>(referrer, runtime->class_loader)))) return object;
  }
  return reinterpret_cast<LookupReferrer>(original_lookup_referrer)(linker, index, referrer);
}
bool FailAllocation(ClassRedefinitionPrefix* self, jvmtiError error, const char* why) noexcept {
  state->rejected_target = Find(mirror_class(self));
  state->rejection_class = nullptr;
  state->rejection = why;
  Field<void*>(current_thread(), runtime->exception) = nullptr;
  self->driver->result = error;
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", why);
  return false;
}
struct HolderView {
  uint8_t* begin = nullptr;
  std::vector<bool>* initialized = nullptr;
  std::vector<bool>* structural = nullptr;
  std::vector<bool>* initial = nullptr;
  ClassRedefinitionPrefix* Definition(int32_t index) const {
    return reinterpret_cast<ClassRedefinitionPrefix*>(begin + index * runtime->redefinition_size);
  }
};
bool VerifyHolder(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, HolderView* output) {
  const auto reject_holder = [&](const char* reason) {
    Disable(reason);
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, reason);
  };
  if (!data->holder || !data->holder->array.reference ||
      !data->holder->array.Get<ObjectArray>() ||
      data->holder->array.Get<ObjectArray>()->length != state->batch_size * RedefinitionDataIter::kNumSlots)
    return reject_holder("interface holder array declaration mismatch");
  auto* definitions = Field<void**>(data->holder, runtime->redefinitions);
  if (!definitions || !definitions[0] || data->index < 0 || data->index >= state->batch_size ||
      reinterpret_cast<uintptr_t>(definitions[1]) != reinterpret_cast<uintptr_t>(definitions[0]) +
          state->batch_size * runtime->redefinition_size ||
      reinterpret_cast<uintptr_t>(definitions[2]) < reinterpret_cast<uintptr_t>(definitions[1]) ||
      reinterpret_cast<uintptr_t>(self) != reinterpret_cast<uintptr_t>(definitions[0]) +
          data->index * runtime->redefinition_size ||
      Slot(*data, 4) != mirror_class(self))
    return reject_holder("interface holder redefinitions declaration mismatch");
  auto& initialized = Field<std::vector<bool>>(data->holder, runtime->initialized);
  auto& structural = Field<std::vector<bool>>(data->holder, runtime->actually_structural);
  auto& initial = Field<std::vector<bool>>(data->holder, runtime->initial_structural);
  if (initialized.size() != static_cast<size_t>(state->batch_size) ||
      structural.size() != initialized.size() || initial.size() != initialized.size())
    return reject_holder("interface holder flag declarations mismatch");
  output->begin = static_cast<uint8_t*>(definitions[0]);
  output->initialized = &initialized;
  output->structural = &structural;
  output->initial = &initial;
  for (jint i = 0; i < state->batch_size; ++i) {
    auto* context = output->Definition(i);
    bool known = false;
    for (jint j = 0; j < state->batch_size; ++j)
      known |= context->klass == state->definitions[j].klass;
    // Never decode an unverified candidate jclass from a guessed element.
    if (!known || context->driver != self->driver)
      return reject_holder("interface holder element prefix declaration mismatch");
    RedefinitionDataIter item{i, data->holder};
    auto* cache = static_cast<DexCache*>(Slot(item, 3));
    if (Slot(item, 4) != mirror_class(context) || !cache ||
        cache->dex_file != reinterpret_cast<uintptr_t>(context->dex_file))
      return reject_holder("interface holder Class/DEX-cache slots declaration mismatch");
  }
  return true;
}
bool AllocateInterface(ClassRedefinitionPrefix* self, RedefinitionDataIter* data,
                       const HolderView& holder) {
  struct AllocationTimer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~AllocationTimer() {
      timing.allocation_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
    }
  } allocation_timer;
  if (!state->array_class || !attempt || attempt->driver != self->driver)
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface redefinition requires private array roots");
  const auto reject_holder = [&](const char* reason) {
    Disable(reason);
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, reason);
  };
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(15)) return reject_holder("interface holder probe rejected injected mismatch");
#endif
  auto& initialized = *holder.initialized;
  auto& structural = *holder.structural;
  auto& initial = *holder.initial;
  if (initialized[data->index] || !structural[data->index] || !initial[data->index])
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface target requires an unallocated structural transaction");
  verified_release.store(state->selected_release);
  const Target* target = Find(mirror_class(self));
  if (!target) return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot target unavailable");
  const auto& ordered = target->snapshot.types;
  if (ordered.empty() || Decode(ordered.front()) != mirror_class(self))
    return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot target is missing or not first");
  for (jclass klass : ordered)
    if (Status(Decode(klass)) < ClassStatus::kResolved)
      return FailAllocation(self, JVMTI_ERROR_INVALID_CLASS, "interface snapshot contains an unresolved class");
  if (ordered.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max() - 7))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition has too many types to root");
  void* thread = self->driver->self;
  if (thread != current_thread())
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface transaction thread mismatch");
  Roots roots(thread, ordered.size() + 7);
  std::vector<Handle> old_types;
  for (jclass klass : ordered) old_types.push_back(roots.Add(Decode(klass)));
  Handle array_class = roots.Add(Decode(state->array_class));
  Handle old_array = roots.Add(allocate_array(thread, array_class.Get<void>(), static_cast<int32_t>(ordered.size())));
  if (!old_array.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition old array allocation failed");
  Handle new_array = roots.Add(allocate_array(thread, array_class.Get<void>(), static_cast<int32_t>(ordered.size())));
  if (!new_array.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition new array allocation failed");
  Assign(ReplacementRoot(*target), new_array.Get<void>());
  for (Handle array : {old_array, new_array})
    if (array.Get<ObjectArray>()->length != static_cast<int32_t>(ordered.size()))
      return reject_holder("interface private array declaration mismatch");
  Handle cache = roots.Add(nullptr), original_cache = roots.Add(nullptr), superclass = roots.Add(nullptr), loader = roots.Add(nullptr);
  std::vector<int32_t> batch_indices(old_types.size(), -1);
  for (size_t i = 0; i < old_types.size(); ++i) {
    for (jint j = 0; j < state->batch_size; ++j) {
      RedefinitionDataIter item{j, data->holder};
      if (Slot(item, 4) != old_types[i].Get<void>()) continue;
      if (initialized[j])
        return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface group was allocated before its owner");
      batch_indices[i] = j;
      // ti_redefine.cc setup_single_redefinition: only the owner collects and
      // commits the group's instances/mappings; child entries retain their DEX.
      structural[j] = true;
      initial[j] = j == data->index;
    }
  }
  for (size_t i = 0; i < old_types.size(); ++i) {
    void* old = old_types[i].Get<void>();
    Store(old_array.Get<ObjectArray>(), static_cast<int32_t>(i), old);
    void* super = Read(Field<uint32_t>(old, runtime->super_class));
    const int32_t parent = target->snapshot.superclass[i];
    if (parent >= 0) {
      if (static_cast<size_t>(parent) >= i || old_types[parent].Get<void>() != super)
        return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot superclass changed");
      super = Read(new_array.Get<ObjectArray>()->elements[parent]);
    }
    Assign(superclass, super);
    Assign(original_cache, Read(Field<uint32_t>(old, runtime->dex_cache)));
    const int32_t batch_index = batch_indices[i];
    RedefinitionDataIter item{batch_index, data->holder};
    auto* allocator = batch_index >= 0 ? holder.Definition(batch_index) : self;
    if (batch_index >= 0) {
      Assign(cache, Slot(item, 3));  // Incoming DEX, rooted by ART's holder.
    } else {
      const auto* dex = reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
      Assign(loader, Read(Field<uint32_t>(old, runtime->class_loader)));
      // CreateNewDexCache reads only driver_ and dex_file_. This non-owning
      // source prefix does not swap or destroy ART's owning DexFile pointer.
      ClassRedefinitionPrefix context{self->driver, nullptr, dex};
      Assign(cache, allocate_cache(&context, loader));
    }
    if (!cache.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition private DexCache allocation failed");
    const DexFilePrefix* expected_dex = batch_index >= 0 ? allocator->dex_file
        : reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
    if (cache.Get<DexCache>()->dex_file != reinterpret_cast<uintptr_t>(expected_dex) ||
        Read(cache.Get<DexCache>()->class_loader) != Read(Field<uint32_t>(old_types[i].Get<void>(), runtime->class_loader)) ||
        cache.Get<void>() == original_cache.Get<void>())
      return reject_holder("interface private DexCache declaration mismatch");
    const Linking scope{&old_types, new_array, cache, static_cast<int32_t>(i)};
    void* replacement;
    {
      LinkingScope active(&scope);
      replacement = allocate_class(allocator, old_types[i], superclass, cache,
          batch_index >= 0 ? 0 : static_cast<uint16_t>(Field<int32_t>(old_types[i].Get<void>(), runtime->dex_class_def_index)));
    }
    if (attempt->error != JVMTI_ERROR_NONE)
      return FailAllocation(self, attempt->error, "interface linking hook failed");
    if (!replacement) return false;  // ART has recorded the error and cleared its exception.
    if (batch_index < 0) set_cache(replacement, original_cache.Get<void>());
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    // Transfer failure after linking, now outside the callback and its locks.
    if (Inject(12)) throw JniFailure{};
#endif
    Store(new_array.Get<ObjectArray>(), static_cast<int32_t>(i), replacement);
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    ++allocated_types;
#endif
    Assign(attempt->pending, nullptr);
    if (batch_index >= 0) {
      SetSlot(item, RedefinitionDataIter::kSlotNewClassObject, replacement);
      initialized[batch_index] = true;
    }
  }
  SetSlot(*data, RedefinitionDataIter::kSlotOldClasses, old_array.Get<void>());
  SetSlot(*data, RedefinitionDataIter::kSlotNewClasses, new_array.Get<void>());
  initialized[data->index] = true;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(2)) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after interface allocation");
  if (++allocated_groups == 2 && Inject(18))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after multiple interface groups allocation");
#endif
  // Run owns cleanup for every subsequent failure, including verification,
  // instance allocation, mapping probes and the subclass-load retry guard.
  return true;
}
const Target* AffectedTarget(void* klass) {
  if (!state) return nullptr;
  for (const auto& target : state->targets)
    for (jclass type : target.snapshot.types)
      if (Decode(type) == klass) return &target;
  return nullptr;
}
bool Finish(ClassRedefinitionPrefix* self, RedefinitionDataHolderPrefix& storage,
            RedefinitionDataIter* data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataHolderPrefix&, RedefinitionDataIter*);
  try {
    const Target* target = Available() ? AffectedTarget(mirror_class(self)) : nullptr;
    if (!target) return reinterpret_cast<Function>(original_finish)(self, storage, data);
    HolderView holder;
    if (!VerifyHolder(self, data, &holder)) return false;
    if ((*holder.initialized)[data->index]) return true;
    // ART sorts by superclass depth, not interface dependencies. The first
    // affected entry delegates the complete group to its interface owner.
    for (jint i = 0; i < state->batch_size; ++i) {
      auto* owner = holder.Definition(i);
      if (owner->klass != target->klass) continue;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
      allocation_order = data->index < i ? 1 : 0;
#endif
      RedefinitionDataIter owner_data{i, data->holder};
      return AllocateInterface(owner, &owner_data, holder);
    }
    return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface group owner is missing from the batch");
  } catch (const std::bad_alloc&) {
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition native allocation failed");
  } catch (...) {
    return FailAllocation(self, ArtHookExceptionError(), "interface allocation hook failed");
  }
}

}  // namespace

void ObserveClassLinker(void* linker) { class_linker.store(linker); }
bool Available() { return available && disabled_reason.load() == nullptr; }
std::string RuntimeState() {
  if (const char* reason = disabled_reason.load()) return std::string("disabled: ") + reason;
  if (!available) return "unavailable";
  const int release = verified_release.load();
  return release ? "verified: android" + std::to_string(release) + " Class/Thread/HandleScope/holder probes"
                 : "unverified: interface runtime declarations";
}
Timing LastTiming() { return timing; }
std::string TakeRejection() {
  if (!state || !state->rejection) return {};
  const char* reason = std::exchange(state->rejection, nullptr);
  const Target* target = state->rejected_target;
  if (state->rejection_class) return *state->rejection_class + ": " + reason;
  return target ? target->live.descriptor + ": " + reason : std::string(reason);
}
bool IsTarget(ClassRedefinitionPrefix* self) { return Available() && Find(mirror_class(self)); }
bool IsAffected(ClassRedefinitionPrefix* self) { return Available() && AffectedTarget(mirror_class(self)); }
bool SharesImplementationGroup(ClassRedefinitionPrefix* self) {
  if (!state) return false;
  for (jclass ancestor : state->shared_ancestors)
    if (Decode(ancestor) == mirror_class(self)) return true;
  return false;
}
void* VerificationClass(void* linker, std::string_view descriptor, Handle loader) noexcept try {
  if (!attempt || !state || state->targets.empty()) return nullptr;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(17)) throw std::bad_alloc{};
#endif
  // LookupClass observes the loader's table without publishing replacements or
  // caching them in any old DEX. The caller limits this to CheckVerification.
  void* old;
  if (lookup_view) {
    old = lookup_view(linker, current_thread(), descriptor, loader.Get<void>());
  } else {
    const std::string name(descriptor);
    old = lookup_chars(linker, current_thread(), name.c_str(), loader.Get<void>());
  }
  for (const auto& target : state->targets) {
    auto* replacements = ReplacementRoot(target).Get<ObjectArray>();
    const auto& types = target.snapshot.types;
    for (size_t i = 0; replacements && i < types.size(); ++i)
      if (Decode(types[i]) == old) return Read(replacements->elements[i]);
  }
  return nullptr;
} catch (...) {
  attempt->error = ArtHookExceptionError();
  return nullptr;
}
jvmtiError VerificationError() noexcept { return attempt ? attempt->error : JVMTI_ERROR_NONE; }
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_configureCheckJni(
    JNIEnv* jni, jclass, jboolean enabled) {
  // Test-only process configuration, before InstallJniGuards captures a table.
  // No private runtime layout or unchecked object access is needed here.
  using namespace symbols;
  constexpr NamedSymbol toggle{Library::kArt, "_ZN3art9JavaVMExt18SetCheckJniEnabledEb", SymbolTable::kAnyNamed};
  constexpr NamedSymbol versions[] = {
      {Library::kArt, "_ZN3art3JNIILb0EE10GetVersionEP7_JNIEnv", SymbolTable::kAnyNamed},
      {Library::kArt, "_ZN3art3JNIILb1EE10GetVersionEP7_JNIEnv", SymbolTable::kAnyNamed}};
  auto set = reinterpret_cast<void (*)(JavaVM*, bool)>(Find(toggle));
  void* plain[] = {Find(versions[0]), Find(versions[1])};
  JavaVM* vm = nullptr;
  if (!set || !plain[0] || !plain[1] || jni->GetJavaVM(&vm) != JNI_OK)
    return JVMTI_ERROR_NOT_AVAILABLE;
  set(vm, enabled);
  void* version = reinterpret_cast<void*>(jni->functions->GetVersion);
  const bool checked = version != plain[0] && version != plain[1];
  return checked == (enabled != 0) ? JVMTI_ERROR_NONE : JVMTI_ERROR_INTERNAL;
}
bool InjectInstanceFailure(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, bool allocated) {
  const Target* owner = Find(mirror_class(self));
  if (!attempt || attempt->driver != self->driver || !owner || owner->snapshot.types.empty()) return false;
  if (!allocated && Inject(3)) {
    FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected instance allocation failure");
    return true;
  }
  if (allocated && Inject(4)) {
    // Force the actual mapping probe to reject the holder, after ART has
    // created new instances. Cleanup roots are independent of the holder.
    SetSlot(*data, RedefinitionDataIter::kSlotNewClasses, nullptr);
  }
  return false;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_primePointerId(
    JNIEnv* jni, jclass, jclass klass, jint kind) {
  // Called before agent initialization. ART may create its own marker when
  // still in swapable-pointer mode; no private heap field is written here.
  uintptr_t id = kind == 0 ? reinterpret_cast<uintptr_t>(jni->GetMethodID(klass, "pointerValue", "()I"))
      : kind == 1 ? reinterpret_cast<uintptr_t>(jni->GetFieldID(klass, "marker", "I"))
                  : reinterpret_cast<uintptr_t>(jni->GetStaticFieldID(klass, "staticMarker", "I"));
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); return -1; }
  return id ? static_cast<jint>((id & 1) == 0) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_armFailure(JNIEnv*, jclass, jint stage) {
  failure_stage = stage;
  failure_hits = 0;
  discarded_classes = 0;
  return available ? 0 : JVMTI_ERROR_NOT_AVAILABLE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_discardedClasses(JNIEnv*, jclass) {
  return discarded_classes;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_allocatedTypes(JNIEnv*, jclass) {
  return allocated_types;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_allocationOrder(JNIEnv*, jclass) {
  return allocation_order;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_failureHits(JNIEnv*, jclass) {
  return failure_hits;
}
#endif
Scope::Scope(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions)
    : state_(new State{state, {}, jni, env, false, nullptr, count, runtime}) {
  auto* next = static_cast<State*>(state_);
  try {
    state = next;
    next->definitions = definitions;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    if (Inject(16)) throw std::bad_alloc{};
#endif
    if (!Available() || count <= 0 || !definitions) return;
    runtime = nullptr;
    void* thread = current_thread();
    for (int release : method_retirement::CandidateReleases(false)) {
      const auto* candidate = RuntimeFor(release);
      if (candidate && VerifyThread(*candidate, thread, jni)) {
        runtime = candidate;
        next->thread_verified = true;
        break;
      }
    }
    for (jint i = 0; i < count; ++i) {
      jint access = 0, status = 0;
      jobject loader = nullptr;
      jclass klass = definitions[i].klass;
      if (!klass || env->GetClassModifiers(klass, &access) != JVMTI_ERROR_NONE || !(access & evolution::kAccInterface)) continue;
      if (env->GetClassLoader(klass, &loader) != JVMTI_ERROR_NONE) continue;
      const auto status_result = env->GetClassStatus(klass, &status);
      const bool boot = !loader;
      if (loader) jni->DeleteLocalRef(loader);
      if (status_result != JVMTI_ERROR_NONE) continue;
      next->targets.push_back({klass, boot, status, {}, {}, nullptr});
    }
    if (!next->targets.empty() && next->thread_verified && jni->EnsureLocalCapacity(8) == JNI_OK) {
      jclass class_class = jni->FindClass("java/lang/Class");
      jobjectArray array = class_class ? jni->NewObjectArray(0, class_class, nullptr) : nullptr;
      if (array) next->array_class = jni->GetObjectClass(array);
      if (array) jni->DeleteLocalRef(array);
      if (class_class) jni->DeleteLocalRef(class_class);
      if (jni->ExceptionCheck()) jni->ExceptionClear();
    }
  } catch (...) {
    if (next->array_class) jni->DeleteLocalRef(next->array_class);
    state = next->previous;
    runtime = next->previous_runtime;
    delete next;
    throw;  // Still Native: Route converts this after restoring both TLS values.
  }
}
const std::vector<jclass>* Scope::GroupTypes(jclass klass) const {
  const auto* current = static_cast<const State*>(state_);
  for (const auto& target : current->targets) {
    if (!target.owner || !current->jni->IsSameObject(target.klass, klass)) continue;
    for (const auto& owner : current->targets)
      if (current->jni->IsSameObject(owner.klass, target.owner)) return &owner.snapshot.types;
  }
  return nullptr;
}
void Scope::Describe(jclass klass, const evolution::ClassSchema& live) {
  auto* next = static_cast<State*>(state_);
  for (auto& target : next->targets)
    if (next->jni->IsSameObject(target.klass, klass)) target.live = live;
}
jvmtiError Scope::Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error) {
  const auto start = std::chrono::steady_clock::now();
  timing = {};
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  allocation_order = 0;
  allocated_types = 0;
  allocated_groups = 0;
#endif
  auto* next = static_cast<State*>(state_);
  // A batch child interface belongs to its highest batch ancestor. Only roots
  // capture a group, so each child interface/implementation is rebuilt once.
  for (auto& target : next->targets) {
    target.owner = target.klass;
    for (const auto& other : next->targets)
      if (next->jni->IsAssignableFrom(target.owner, other.klass)) target.owner = other.klass;
    if (next->jni->ExceptionCheck()) {
      next->jni->ExceptionClear();
      *error = target.live.descriptor + ": interface batch ownership check failed";
      return JVMTI_ERROR_INTERNAL;
    }
  }
  for (auto& target : next->targets) {
    if (!next->jni->IsSameObject(target.owner, target.klass)) continue;
    jvmtiError result = SnapshotTypes(next->jni, next->env, target.klass,
                                      count, loaded, &target.snapshot, error);
    if (result != JVMTI_ERROR_NONE) return result;
    timing.types += target.snapshot.types.size();
  }
  // SnapshotTypes borrows the exact GetLoadedClasses references. Identity
  // keys here detect overlap in one pass without pairwise type comparisons.
  std::unordered_map<jclass, const Target*> owners;
  for (const auto& target : next->targets) {
    for (jclass type : target.snapshot.types) {
      if (!owners.emplace(type, &target).second) {
        *error = target.live.descriptor + ": unrelated interface groups share a subtype";
        return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
      }
    }
  }
  for (const auto& target : next->targets) {
    for (jint i = 0; i < next->batch_size; ++i) {
      jclass other = next->definitions[i].klass;
      if (next->jni->IsAssignableFrom(other, target.klass)) continue;
      // A class outside the interface's type relation can still share its
      // implementation subtree. Two structural owners would rebuild it twice.
      for (jclass type : target.snapshot.types) {
        if (!next->jni->IsAssignableFrom(type, other)) continue;
        next->shared_ancestors.push_back(other);
        break;
      }
      if (next->jni->ExceptionCheck()) {
        next->jni->ExceptionClear();
        *error = target.live.descriptor + ": interface batch group-overlap check failed";
        return JVMTI_ERROR_INTERNAL;
      }
    }
  }
  if (!next->targets.empty() && next->jni->EnsureLocalCapacity(8) != JNI_OK) {
    if (next->jni->ExceptionCheck()) next->jni->ExceptionClear();
    *error = "interface snapshot local reference reservation failed";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  if (!next->targets.empty())
    timing.snapshot_ns = loaded_ns + std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
  return JVMTI_ERROR_NONE;
}
Scope::~Scope() {
  auto* old = static_cast<State*>(state_);
  if (old->array_class) old->jni->DeleteLocalRef(old->array_class);
  state = old->previous;
  runtime = old->previous_runtime;
  delete old;
}
void Prepare(std::vector<Hook>* hooks, std::string* note) {
  available = false;
  disabled_reason.store(nullptr);
  verified_release.store(0);
  const auto reject = [&](const char* reason) {
    disabled_reason.store(reason);
    *note = reason;
  };
  const int api = android_get_device_api_level();
  if (api != 33 && api != 36) {
    reject("interface redefinition is device-verified only on Android 13/API 33 and Android 16/API 36");
    return;
  }
  if (!method_retirement::SuperclassChangeAvailable()) {
    reject("interface redefinition requires verifier hooks and runtime-probed declarations");
    return;
  }
  using namespace symbols::interface_redefinition;
  current_thread = reinterpret_cast<decltype(current_thread)>(symbols::Find(kCurrentThread));
  visit_handle_roots = reinterpret_cast<decltype(visit_handle_roots)>(symbols::Find(kHandleScopeVisitRoots));
  mirror_class = reinterpret_cast<Mirror>(symbols::Find(symbols::structural::kClassRedefinitionGetMirrorClass));
  find_instance_field = reinterpret_cast<decltype(find_instance_field)>(symbols::Find(symbols::structural::kFindInstanceField));
  ensure_initialized = reinterpret_cast<Ensure>(symbols::Find(kEnsureInitialized));
  new_local_ref = reinterpret_cast<decltype(new_local_ref)>(symbols::Find(kNewLocalRef));
  delete_local_ref = reinterpret_cast<decltype(delete_local_ref)>(symbols::Find(kDeleteLocalRef));
  allocate_cache = reinterpret_cast<CacheAlloc>(symbols::Find(kCreateNewDexCache));
  allocate_array = reinterpret_cast<ArrayAlloc>(symbols::Find(kClassArrayAlloc));
  if (!allocate_array) allocate_array = reinterpret_cast<ArrayAlloc>(symbols::Find(kObjectArrayAlloc));
  set_cache = reinterpret_cast<SetCache>(symbols::Find(kSetDexCache));
  allocate_class = reinterpret_cast<Allocate>(symbols::Find(symbols::structural::kAllocateNewClassObject));
  lookup_chars = reinterpret_cast<LookupChars>(symbols::Find(kLookupClassChars));
  lookup_view = reinterpret_cast<LookupView>(symbols::Find(kLookupClassView));
  mark = reinterpret_cast<decltype(mark)>(symbols::Find(kReadBarrierMark));
  void* referrer_lookup = symbols::Find(kLookupReferrer);
  void* cache_lookup = symbols::Find(kLookupCache);
  void* finish = symbols::Find(kFinishNewClassAllocations);
  void* run = symbols::Find(kRun);
  void* link_class = symbols::Find(kLinkClass);
  set_status_internal = reinterpret_cast<decltype(set_status_internal)>(symbols::Find(kSetStatusInternal));
  if (!current_thread || !visit_handle_roots || !mirror_class || !find_instance_field || !ensure_initialized || !new_local_ref || !delete_local_ref || !finish ||
      !allocate_cache || !allocate_array || !set_cache || !allocate_class || (!lookup_chars && !lookup_view) || !mark ||
      !referrer_lookup || !cache_lookup || !run || !link_class || !set_status_internal) {
    reject("interface redefinition named services unavailable");
    return;
  }
  std::vector<Hook> resolved;
  const symbols::NamedSymbol names[] = {kCanNormal, kGetNormal, kCanStructural, kGetStructural};
  void* replacements[] = {reinterpret_cast<void*>(NormalCan), reinterpret_cast<void*>(NormalGet),
      reinterpret_cast<void*>(StructuralCan), reinterpret_cast<void*>(StructuralGet)};
  bool modes[2]{};
  for (size_t i = 0; i < 4; ++i) {
    if (void* address = symbols::Find(names[i])) {
      resolved.push_back({address, replacements[i], i < 2 ? &original_normal[i] : &original_structural[i - 2], names[i].name});
      modes[i / 2] = true;
    }
  }
  if (!modes[0] || !modes[1]) { reject("interface redefinition admission names unavailable"); return; }
  resolved.push_back({finish, reinterpret_cast<void*>(Finish), &original_finish, kFinishNewClassAllocations.name});
  resolved.push_back({run, reinterpret_cast<void*>(Run), &original_run, kRun.name});
  resolved.push_back({link_class, reinterpret_cast<void*>(LinkClass), &original_link_class, kLinkClass.name});
  resolved.push_back({referrer_lookup, reinterpret_cast<void*>(LookupByReferrer), &original_lookup_referrer, kLookupReferrer.name});
  resolved.push_back({cache_lookup, reinterpret_cast<void*>(LookupByCache), &original_lookup_cache, kLookupCache.name});
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  available = true;
  *note = "interface redefinition prepared; Class/Thread/HandleScope/holder runtime probes required";
}
}  // namespace artdcevm::interface_redefinition

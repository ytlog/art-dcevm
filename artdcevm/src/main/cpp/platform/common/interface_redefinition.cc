#include "platform/common/interface_redefinition.h"

#include <android/api-level.h>
#include <android/log.h>
#include <atomic>
#include <algorithm>
#include <memory>
#include <limits>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <string_view>
#include "platform/abi/arm64/android13/interface_runtime.h"
#include "platform/abi/arm64/android16/interface_runtime.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/common/class_linker.h"
#include "platform/abi/arm64/android13/redefinition.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/art_hook.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::interface_redefinition {
namespace {
using namespace abi::arm64;
const InterfaceRuntime* runtime = nullptr;
template <class T> T& Field(void* object, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
uint32_t Flags(void* klass) { return Field<uint32_t>(klass, runtime->access_flags); }
using Admission = jvmtiError (*)(Handle, std::string*);
using Mirror = void* (*)(ClassRedefinitionPrefix*);
using Ensure = bool (*)(void*, void*, Handle, bool, bool);
using Visit = void (*)(void*, ClassVisitor*);
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
Ensure ensure_initialized = nullptr;
jobject (*new_local_ref)(JNIEnv*, void*) = nullptr;
void (*delete_local_ref)(JNIEnv*, jobject) = nullptr;
Visit visit_classes = nullptr;
std::atomic<void*> class_linker{nullptr};
void* original_normal[2]{};
void* original_structural[2]{};
void* original_finish = nullptr;
void* original_run = nullptr;
void* original_link_class = nullptr;
void (*set_status_internal)(void*, android13::ClassStatus) = nullptr;
bool available = false;
struct Target { jclass klass; bool boot; jint access; jint status; };
struct State { State* previous; std::vector<Target> targets; JNIEnv* jni; bool thread_verified; jclass array_class; jint batch_size; };
thread_local State* state = nullptr;
struct JniFailure {};
class LocalRef {
 public:
  explicit LocalRef(void* object) : reference_(new_local_ref(state->jni, object)) {}
  ~LocalRef() { delete_local_ref(state->jni, reference_); }
  jobject Get() const { return reference_; }
 private:
  jobject reference_;
};
bool Assignable(void* target, void* candidate) {
  LocalRef super(target), sub(candidate);
  const bool result = state->jni->IsAssignableFrom(
      static_cast<jclass>(sub.Get()), static_cast<jclass>(super.Get()));
  if (state->jni->ExceptionCheck()) throw JniFailure{};
  return result;
}
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
thread_local int failure_stage = 0;
thread_local int discarded_classes = 0;
thread_local int failure_hits = 0;
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
  if (!state || !state->thread_verified) return nullptr;
  for (const auto& target : state->targets)
    if (Decode(target.klass) == klass) return &target;
  return nullptr;
}
uint32_t Status(void* klass) {
  // mirror/class.h: status occupies the four most significant bits.
  return __atomic_load_n(&Field<uint32_t>(klass, runtime->status), __ATOMIC_ACQUIRE) >> 28;
}
jvmtiError Reject(std::string* message, jvmtiError error, const char* why) {
  *message = why;
  return error;
}
struct ProxyVisitor final : ClassVisitor {
  void* target;
  bool found = false;
  jvmtiError error = JVMTI_ERROR_NONE;
  explicit ProxyVisitor(void* klass) : target(klass) {}
  bool operator()(void* klass) noexcept override try {
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    if (Inject(6)) throw std::bad_alloc{};
    if (Inject(7)) throw JniFailure{};
#endif
    if ((Flags(klass) & 0x00040000u) && Assignable(target, klass)) { found = true; return false; }
    return true;
  } catch (...) {
    error = ArtHookExceptionError();
    return false;  // VisitClasses must return normally through ART's frames.
  }
};
jvmtiError Admit(Handle klass, std::string* message, bool structural, size_t spelling) noexcept try {
  void* original = structural ? original_structural[spelling] : original_normal[spelling];
  void* object = klass.Get<void>();
  const Target* target = Find(object);
  if (state && !state->targets.empty())
    __android_log_print(ANDROID_LOG_INFO, "ART-DCEVM", "interface admission hook: structural=%d target=%d", structural, target != nullptr);
  if (!target) return reinterpret_cast<Admission>(original)(klass, message);
  // Check this source prefix against the public snapshot before using its tail.
  // Nested interfaces with different InnerClass flags are outside this spike.
  if ((Flags(object) & kAccJavaFlagsMask) !=
          (static_cast<uint32_t>(target->access) & kAccJavaFlagsMask) ||
      ((Status(object) >= 14) != ((target->status & JVMTI_CLASS_STATUS_INITIALIZED) != 0)))
    return Reject(message, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface spike class prefix mismatch");
  if (target->boot)
    return Reject(message, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface spike rejects boot interfaces");
  void* linker = class_linker.load();
  if (!linker)
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, "interface spike ClassLinker unavailable");
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(8)) throw std::bad_alloc{};
#endif
  ProxyVisitor proxies(object);
  visit_classes(linker, &proxies);
  if (proxies.error != JVMTI_ERROR_NONE) {
    Field<void*>(current_thread(), runtime->exception) = nullptr;
    return proxies.error;
  }
  if (proxies.found)
    return Reject(message, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface spike rejects Proxy implementations");
  if (structural) {
    void* thread = current_thread();
    if (Status(object) == 13 && Field<uint32_t>(object, runtime->clinit_thread_id) == Field<uint32_t>(thread, runtime->tid))
      return Reject(message, JVMTI_ERROR_INTERNAL, "interface spike rejects redefinition during initialization");
    if (!ensure_initialized(linker, thread, klass, true, true)) {
      // Thread::ClearException() in runtime/thread.h is this assignment.
      Field<void*>(thread, runtime->exception) = nullptr;
      return Reject(message, JVMTI_ERROR_INVALID_CLASS, "interface spike target failed initialization");
    }
  }
  return JVMTI_ERROR_NONE;
} catch (...) {
  // Even writing the diagnostic string can allocate. Do not write one here.
  Field<void*>(current_thread(), runtime->exception) = nullptr;
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
  // the ART collector operation; the spike does not implement a collector.
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
struct Attempt { RedefinerPrefix* driver; Handle replacements; Handle pending; bool committed = false; jvmtiError error = JVMTI_ERROR_NONE; };
thread_local Attempt* attempt = nullptr;
void DiscardClass(void* klass) {
  if (!klass) return;
  const uint32_t status = Status(klass);
  if (status >= static_cast<uint32_t>(android13::ClassStatus::kResolved) ||
      status == static_cast<uint32_t>(android13::ClassStatus::kErrorResolved)) {
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
  auto* classes = next.replacements.Get<ObjectArray>();
  for (int32_t i = 0; classes && i < classes->length; ++i)
    DiscardClass(Read(classes->elements[i]));
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
  if (!state || !state->thread_verified || state->targets.empty())
    return reinterpret_cast<Function>(original_run)(driver);
  try {
    Roots roots(driver->self, 2);
    Attempt next{driver, roots.Add(nullptr), roots.Add(nullptr)};
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
struct Subtypes final : ClassVisitor {
  void* target;
  std::vector<void*> classes;
  bool proxy = false;
  jvmtiError error = JVMTI_ERROR_NONE;
  explicit Subtypes(void* klass) : target(klass) {}
  bool operator()(void* klass) noexcept override try {
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    if (Inject(9)) throw std::bad_alloc{};
    if (Inject(10)) throw JniFailure{};
#endif
    if (Status(klass) >= 7 && Assignable(target, klass)) {
      proxy |= (Flags(klass) & 0x00040000u) != 0;
      classes.push_back(klass);
    }
    return true;
  } catch (...) {
    error = ArtHookExceptionError();
    return false;
  }
};
bool FailAllocation(ClassRedefinitionPrefix* self, jvmtiError error, const char* why) noexcept {
  Field<void*>(current_thread(), runtime->exception) = nullptr;
  self->driver->result = error;
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", why);
  return false;
}
bool AllocateInterface(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) {
  if (state->batch_size != 1 || !state->array_class || !attempt || attempt->driver != self->driver)
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface spike requires one target and array root");
  auto& initialized = Field<std::vector<bool>>(data->holder, runtime->initialized);
  auto& structural = Field<std::vector<bool>>(data->holder, runtime->actually_structural);
  auto& initial = Field<std::vector<bool>>(data->holder, runtime->initial_structural);
  if (data->index != 0 || initialized.size() != 1 || structural.size() != 1 || initial.size() != 1 ||
      initialized[0] || !structural[0] || !initial[0])
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface spike holder flags mismatch");
  void* linker = class_linker.load();
  Subtypes subtypes(mirror_class(self));
  visit_classes(linker, &subtypes);
  if (subtypes.error != JVMTI_ERROR_NONE)
    return FailAllocation(self, subtypes.error, "interface subtype traversal failed");
  if (subtypes.proxy)
    return FailAllocation(self, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface spike found a Proxy after class loading paused");
  // Kahn order over assignability: superclass and superinterfaces precede each
  // subtype. A partial-order comparator cannot safely be used with std::sort.
  std::vector<void*> ordered;
  while (!subtypes.classes.empty()) {
    auto ready = std::find_if(subtypes.classes.begin(), subtypes.classes.end(), [&](void* candidate) {
      for (void* other : subtypes.classes)
        if (other != candidate && Assignable(other, candidate)) return false;
      return true;
    });
    if (ready == subtypes.classes.end())
      return FailAllocation(self, JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION, "interface spike subtype order is cyclic");
    ordered.push_back(*ready);
    subtypes.classes.erase(ready);
  }
  if (ordered.empty() || ordered.front() != mirror_class(self))
    return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface spike target is missing from subtype order");
  if (ordered.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max() - 7))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface spike has too many types to root");
  void* thread = self->driver->self;
  if (thread != current_thread())
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface transaction thread mismatch");
  Roots roots(thread, ordered.size() + 7);
  std::vector<Handle> old_types;
  for (void* object : ordered) old_types.push_back(roots.Add(object));
  Handle array_class = roots.Add(Decode(state->array_class));
  Handle old_array = roots.Add(allocate_array(thread, array_class.Get<void>(), static_cast<int32_t>(ordered.size())));
  if (!old_array.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface spike old array allocation failed");
  Handle new_array = roots.Add(allocate_array(thread, array_class.Get<void>(), static_cast<int32_t>(ordered.size())));
  if (!new_array.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface spike new array allocation failed");
  Assign(attempt->replacements, new_array.Get<void>());
  for (Handle array : {old_array, new_array})
    if (array.Get<ObjectArray>()->length != static_cast<int32_t>(ordered.size()))
      return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface private array prefix mismatch");
  Handle cache = roots.Add(nullptr), original_cache = roots.Add(nullptr), superclass = roots.Add(nullptr), loader = roots.Add(nullptr);
  for (size_t i = 0; i < old_types.size(); ++i) {
    void* old = old_types[i].Get<void>();
    Store(old_array.Get<ObjectArray>(), static_cast<int32_t>(i), old);
    void* super = Read(Field<uint32_t>(old, runtime->super_class));
    for (size_t j = 0; j < i; ++j)
      if (old_types[j].Get<void>() == super) { super = Read(new_array.Get<ObjectArray>()->elements[j]); break; }
    Assign(superclass, super);
    Assign(original_cache, Read(Field<uint32_t>(old, runtime->dex_cache)));
    if (i == 0) {
      Assign(cache, Slot(*data, 3));  // kSlotNewDexCache, rooted by ART's holder.
    } else {
      const auto* dex = reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
      Assign(loader, Read(Field<uint32_t>(old, runtime->class_loader)));
      // CreateNewDexCache reads only driver_ and dex_file_. This non-owning
      // source prefix does not swap or destroy ART's owning DexFile pointer.
      ClassRedefinitionPrefix context{self->driver, nullptr, dex};
      Assign(cache, allocate_cache(&context, loader));
    }
    if (!cache.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface spike private DexCache allocation failed");
    const DexFilePrefix* expected_dex = i == 0 ? self->dex_file
        : reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
    if (cache.Get<DexCache>()->dex_file != reinterpret_cast<uintptr_t>(expected_dex) ||
        Read(cache.Get<DexCache>()->class_loader) != Read(Field<uint32_t>(old_types[i].Get<void>(), runtime->class_loader)) ||
        cache.Get<void>() == original_cache.Get<void>())
      return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface private DexCache prefix mismatch");
    const Linking scope{&old_types, new_array, cache, static_cast<int32_t>(i)};
    void* replacement;
    {
      LinkingScope active(&scope);
      replacement = allocate_class(self, old_types[i], superclass, cache,
          i == 0 ? 0 : static_cast<uint16_t>(Field<int32_t>(old_types[i].Get<void>(), runtime->dex_class_def_index)));
    }
    if (attempt->error != JVMTI_ERROR_NONE)
      return FailAllocation(self, attempt->error, "interface linking hook failed");
    if (!replacement) return false;  // ART has recorded the error and cleared its exception.
    if (i != 0) set_cache(replacement, original_cache.Get<void>());
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
    // Transfer failure after linking, now outside the callback and its locks.
    if (Inject(12)) throw JniFailure{};
#endif
    Store(new_array.Get<ObjectArray>(), static_cast<int32_t>(i), replacement);
    Assign(attempt->pending, nullptr);
    if (i == 0) SetSlot(*data, RedefinitionDataIter::kSlotNewClassObject, replacement);
  }
  SetSlot(*data, RedefinitionDataIter::kSlotOldClasses, old_array.Get<void>());
  SetSlot(*data, RedefinitionDataIter::kSlotNewClasses, new_array.Get<void>());
  initialized[0] = true;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (Inject(2)) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after interface allocation");
#endif
  __android_log_print(ANDROID_LOG_INFO, "ART-DCEVM", "interface spike allocated %zu types with private link caches", old_types.size());
  // Run owns cleanup for every subsequent failure, including verification,
  // instance allocation, mapping probes and the subclass-load retry guard.
  return true;
}
bool Finish(ClassRedefinitionPrefix* self, RedefinitionDataHolderPrefix& holder,
            RedefinitionDataIter* data) noexcept {
  if (!IsTarget(self)) {
    using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataHolderPrefix&, RedefinitionDataIter*);
    return reinterpret_cast<Function>(original_finish)(self, holder, data);
  }
  try { return AllocateInterface(self, data); }
  catch (const std::bad_alloc&) {
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface spike native allocation failed");
  } catch (...) {
    return FailAllocation(self, ArtHookExceptionError(), "interface allocation hook failed");
  }
}

}  // namespace

void ObserveClassLinker(void* linker) { class_linker.store(linker); }
bool Available() { return available; }
bool IsTarget(ClassRedefinitionPrefix* self) { return available && Find(mirror_class(self)); }
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
  __android_log_print(ANDROID_LOG_INFO, "ART-DCEVM", "test JNI mode: checked=%d requested=%d", checked, enabled != 0);
  return checked == (enabled != 0) ? JVMTI_ERROR_NONE : JVMTI_ERROR_INTERNAL;
}
bool InjectInstanceFailure(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, bool allocated) {
  if (!attempt || attempt->driver != self->driver || !IsTarget(self)) return false;
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
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_failureHits(JNIEnv*, jclass) {
  return failure_hits;
}
#endif
Scope::Scope(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions)
    : state_(new State{state, {}, jni, false, nullptr, count}) {
  auto* next = static_cast<State*>(state_);
  state = next;
  if (!available || count <= 0 || !definitions) return;
  void* thread = current_thread();
  // tlsPtr_.self is not populated on arm64; CurrentFromGdb supplies self.
  next->thread_verified = thread && Field<void*>(thread, runtime->jni_env) == jni &&
      Field<uint32_t>(thread, runtime->tid) == static_cast<uint32_t>(syscall(__NR_gettid));
  for (jint i = 0; i < count; ++i) {
    jint access = 0, status = 0;
    jobject loader = nullptr;
    jclass klass = definitions[i].klass;
    if (!klass || env->GetClassModifiers(klass, &access) != JVMTI_ERROR_NONE || !(access & 0x0200)) continue;
    if (env->GetClassLoader(klass, &loader) != JVMTI_ERROR_NONE ||
        env->GetClassStatus(klass, &status) != JVMTI_ERROR_NONE) continue;
    next->targets.push_back({klass, !loader, access, status});
    if (loader) jni->DeleteLocalRef(loader);
  }
  if (!next->targets.empty() && next->thread_verified && jni->EnsureLocalCapacity(8) == JNI_OK) {
    jclass class_class = jni->FindClass("java/lang/Class");
    jobjectArray array = class_class ? jni->NewObjectArray(0, class_class, nullptr) : nullptr;
    if (array) next->array_class = jni->GetObjectClass(array);
    if (array) jni->DeleteLocalRef(array);
    if (class_class) jni->DeleteLocalRef(class_class);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
}
Scope::~Scope() {
  auto* old = static_cast<State*>(state_);
  if (old->array_class) old->jni->DeleteLocalRef(old->array_class);
  state = old->previous;
  delete old;
}
void Prepare(std::vector<method_retirement::Hook>* hooks, std::string* note) {
  available = false;
  const int api = android_get_device_api_level();
  runtime = api == 33 ? &android13::kInterfaceRuntime : api == 36 ? &android16::kInterfaceRuntime : nullptr;
  if (!runtime || !method_retirement::SuperclassChangeAvailable()) {
    *note = "interface spike requires tested API 33/36 declarations and verifier hooks";
    return;
  }
  using namespace symbols::interface_redefinition;
  current_thread = reinterpret_cast<decltype(current_thread)>(symbols::Find(kCurrentThread));
  mirror_class = reinterpret_cast<Mirror>(symbols::Find(symbols::structural::kClassRedefinitionGetMirrorClass));
  ensure_initialized = reinterpret_cast<Ensure>(symbols::Find(kEnsureInitialized));
  visit_classes = reinterpret_cast<Visit>(symbols::Find(kVisitClasses));
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
  if (!current_thread || !mirror_class || !ensure_initialized || !visit_classes || !new_local_ref || !delete_local_ref || !finish ||
      !allocate_cache || !allocate_array || !set_cache || !allocate_class || (!lookup_chars && !lookup_view) || !mark ||
      !referrer_lookup || !cache_lookup || !run || !link_class || !set_status_internal) {
    *note = "interface spike named services unavailable";
    return;
  }
  std::vector<method_retirement::Hook> resolved;
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
  if (!modes[0] || !modes[1]) { *note = "interface spike admission names unavailable"; return; }
  resolved.push_back({finish, reinterpret_cast<void*>(Finish), &original_finish, kFinishNewClassAllocations.name});
  resolved.push_back({run, reinterpret_cast<void*>(Run), &original_run, kRun.name});
  resolved.push_back({link_class, reinterpret_cast<void*>(LinkClass), &original_link_class, kLinkClass.name});
  resolved.push_back({referrer_lookup, reinterpret_cast<void*>(LookupByReferrer), &original_lookup_referrer, kLookupReferrer.name});
  resolved.push_back({cache_lookup, reinterpret_cast<void*>(LookupByCache), &original_lookup_cache, kLookupCache.name});
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  available = true;
  *note = "interface spike private allocation enabled with source-declared runtime layout";
}
}  // namespace artdcevm::interface_redefinition

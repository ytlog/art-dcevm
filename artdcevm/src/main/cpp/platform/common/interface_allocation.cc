#include "platform/common/interface_redefinition_internal.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
namespace detail {
// Match HandleScope::Create/Thread::PushHandleScope rather than borrowing
// unrooted raw pointers across ART allocations. No Java allocation occurs while
// gathering the Class objects or constructing this C++ storage.
class Roots {
 public:
  Roots(void* thread, size_t count)
      : thread_(thread), runtime_(Runtime()), storage_(new uint8_t[runtime_.scope_header_size + 4 * count]{}) {
    Field<void*>(storage_.get(), runtime_.scope_link) = Field<void*>(thread_, ThreadLayout().top_handle_scope);
    Field<int32_t>(storage_.get(), runtime_.scope_capacity) = static_cast<int32_t>(count);
    Field<void*>(thread_, ThreadLayout().top_handle_scope) = storage_.get();
  }
  ~Roots() { Field<void*>(thread_, ThreadLayout().top_handle_scope) = Field<void*>(storage_.get(), runtime_.scope_link); }
  Roots(const Roots&) = delete;
  Roots& operator=(const Roots&) = delete;
  Handle Add(void* object) {
    auto* slot = reinterpret_cast<uint32_t*>(storage_.get() + runtime_.scope_header_size) + used_++;
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    if (runtime_.scope_size != InterfaceRuntime::kAbsent)
      Field<uint32_t>(storage_.get(), runtime_.scope_size) = static_cast<uint32_t>(used_);
    return {slot};
  }
 private:
  void* thread_;
  const InterfaceRuntime& runtime_;
  std::unique_ptr<uint8_t[]> storage_;
  size_t used_ = 0;
};
void Assign(Handle handle, void* object) {
  *const_cast<uint32_t*>(handle.reference) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
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
  JNIEnv* jni = CurrentState()->jni;
  jni->SetObjectArrayElement(static_cast<jobjectArray>(destination.Get()), index, element.Get());
  if (jni->ExceptionCheck()) throw JniFailure{};
}
void* Slot(const RedefinitionDataIter& data, int32_t slot) {
  return runtime_thread::Read(Thread(), data.holder->array.Get<ObjectArray>()->elements[data.index * RedefinitionDataIter::kNumSlots + slot]);
}
void SetSlot(const RedefinitionDataIter& data, int32_t slot, void* object) {
  Store(data.holder->array.Get<ObjectArray>(), data.index * RedefinitionDataIter::kNumSlots + slot, object);
}
Handle ReplacementRoot(const Target& target) {
  const State* state = CurrentState();
  return state->attempt->replacements[&target - state->targets.data()];
}
void DiscardClass(void* klass) {
  if (!klass) return;
  const ClassStatus status = Status(klass);
  if (status >= ClassStatus::kResolved ||
      status == ClassStatus::kErrorResolved) {
    // Unpublished classes have no copied statics or initializer to preserve.
    // kErrorResolved would still enter the next heap subtype scan.
    process->set_status_internal(klass, ClassStatus::kErrorUnresolved);
ARTDCEVM_TEST(
    fault_injection::Increment(fault_injection::Counter::kDiscardedClasses);
)
  }
}
void Discard(const Attempt& next) {
  if (next.committed) return;
  // A linked class can outlive a failed allocator without reaching its array.
  DiscardClass(next.pending.Get<void>());
  for (Handle root : next.replacements) {
    auto* classes = root.Get<ObjectArray>();
    for (int32_t i = 0; classes && i < classes->length; ++i)
      DiscardClass(runtime_thread::Read(Thread(), classes->elements[i]));
  }
}

class AttemptScope {
 public:
  AttemptScope(State* state, Attempt* next) : state_(state), previous_(std::exchange(state->attempt, next)) {}
  ~AttemptScope() { process->backend->Discard(); state_->attempt = previous_; }
 private:
  State* state_;
  Attempt* previous_;
};
jvmtiError Run(RedefinerPrefix* driver) noexcept {
  using Function = jvmtiError (*)(RedefinerPrefix*);
  State* state = CurrentState();
  if (!state || state->targets.empty())
    return reinterpret_cast<Function>(process->original_run)(driver);
  if (!runtime_thread::Ready() || !state->runtime_verified) {
    driver->result = JVMTI_ERROR_NOT_AVAILABLE;
    state->rejection = "interface transaction entered Run without completed runtime probes";
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  if (driver->self != runtime_thread::Current(Thread())) {
    Disable("interface Redefiner Thread declaration mismatch");
    state->rejection = process->disabled_reason.load();
    driver->result = JVMTI_ERROR_NOT_AVAILABLE;
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  try {
    Roots roots(driver->self, state->targets.size() + 1);
    Attempt next{driver, {}, roots.Add(nullptr), false, JVMTI_ERROR_NONE, {}};
    for (size_t i = 0; i < state->targets.size(); ++i) next.replacements.push_back(roots.Add(nullptr));
    AttemptScope scope(state, &next);
ARTDCEVM_TEST(
    fault_injection::Set(fault_injection::Counter::kValidatedGroups, 0);
)
    const jvmtiError result = reinterpret_cast<Function>(process->original_run)(driver);
    next.committed = result == JVMTI_ERROR_NONE;
    return result;
  } catch (const std::bad_alloc&) {
    // Only this hook's root allocation may throw here; deeper hooks catch
    // at their own boundary, before returning through ART.
    driver->result = JVMTI_ERROR_OUT_OF_MEMORY;
    return JVMTI_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    Field<void*>(driver->self, ThreadLayout().exception) = nullptr;
    driver->result = ArtHookExceptionError();
    return static_cast<jvmtiError>(driver->result);
  }
}
// Observe ART's actual selected pair, preserving the original exception. No
// formatting, JNI, allocation or method lookup is allowed in this callback.
void NonPublicImplementation(void* klass, ArtMethod* implementation, ArtMethod* declaration) noexcept {
  State* state = CurrentState();
  if (state && state->linking && state->attempt && state->attempt->pending.Get<void>() == klass)
    state->attempt->non_public = {{implementation, declaration}, state->linking->index};
  using Function = void (*)(void*, ArtMethod*, ArtMethod*);
  reinterpret_cast<Function>(process->original_non_public)(klass, implementation, declaration);
}

bool LinkClass(void* linker, void* thread, const char* descriptor, Handle klass,
               Handle interfaces, Handle* output) noexcept {
  using Function = bool (*)(void*, void*, const char*, Handle, Handle, Handle*);
  State* state = CurrentState();
  const Linking* linking = state ? state->linking : nullptr;
  if (linking && !descriptor) Assign(state->attempt->pending, klass.Get<void>());
  const bool result = reinterpret_cast<Function>(process->original_link_class)(
      linker, thread, descriptor, klass, interfaces, output);
  // Capture the resolved class before AllocateNewClassObject's JNI-ID array
  // allocations. Those can fail after linking and return nullptr to Finish.
  // This callback may run with ClassLinker locks held. Only assign an existing
  // HandleScope root: no JNI/local-table growth, heap store or allocation here.
  if (linking && !descriptor && output && output->Get<void>())
    Assign(state->attempt->pending, output->Get<void>());
ARTDCEVM_TEST(
  // A link failure after producing the class exercises the allocator's null
  // return path, before Finish has recorded this replacement in the holder.
  if (result && linking && linking->index == 1 && ARTDCEVM_FAULT(1)) return false;
)
  return result;
}
class LinkingScope {
 public:
  LinkingScope(State* state, const Linking* scope) : state_(state), previous_(std::exchange(state->linking, scope)) {}
  ~LinkingScope() { state_->linking = previous_; }
 private:
  State* state_;
  const Linking* previous_;
};
void* Redirect(void* linker, TypeIndex index, void* cache, void* loader) noexcept try {
  const State* state = CurrentState();
  const Linking* linking = state ? state->linking : nullptr;
  if (!linking || cache != linking->private_cache.Get<void>()) return nullptr;
  const auto* dex = reinterpret_cast<const DexFilePrefix*>(static_cast<DexCache*>(cache)->dex_file);
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(11)) throw std::bad_alloc{};
)
  std::string type, error;
  if (!dex || !evolution::ReadTypeDescriptor(DexViewOf(*dex), index.index, &type, &error)) return nullptr;
  // Unlike DoLookupResolvedType, descriptor lookup does not write the cache.
  // A redirected entry must remain empty, so later inline cache probes cannot
  // find an old interface and bypass this hook during the same link.
  void* old = process->lookup_view ? process->lookup_view(linker, runtime_thread::Current(Thread()), type, loader)
                         : process->lookup_chars(linker, runtime_thread::Current(Thread()), type.c_str(), loader);
  for (size_t i = 0; i < linking->old_types->size(); ++i)
    if ((*linking->old_types)[i].Get<void>() == old)
      return runtime_thread::Read(Thread(), linking->new_types.Get<ObjectArray>()->elements[i]);
  return nullptr;
} catch (...) {
  // Lookup cannot return a JVMTI error. Retain it until Finish regains control;
  // the result of this link will never be committed.
  CurrentState()->attempt->error = ArtHookExceptionError();
  return nullptr;
}
void* LookupByCache(void* linker, TypeIndex index, void* cache, void* loader) noexcept {
  if (void* object = Redirect(linker, index, cache, loader)) return object;
  return reinterpret_cast<LookupCache>(process->original_lookup_cache)(linker, index, cache, loader);
}
void* LookupByReferrer(void* linker, TypeIndex index, void* referrer) noexcept {
  if (const State* state = CurrentState(); state && state->linking) {
    if (void* object = Redirect(linker, index, runtime_thread::Read(Thread(), Field<uint32_t>(referrer, state->runtime->dex_cache)),
                               runtime_thread::Read(Thread(), Field<uint32_t>(referrer, state->runtime->class_loader)))) return object;
  }
  return reinterpret_cast<LookupReferrer>(process->original_lookup_referrer)(linker, index, referrer);
}
bool FailAllocation(ClassRedefinitionPrefix* self, jvmtiError error, const char* why) noexcept {
  State* state = CurrentState();
  state->rejected_target = Find(process->mirror_class(self));
  state->rejection_class = nullptr;
  state->rejection = why;
  Field<void*>(runtime_thread::Current(Thread()), ThreadLayout().exception) = nullptr;
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
    return reinterpret_cast<ClassRedefinitionPrefix*>(begin + index * Runtime().redefinition_size);
  }
};
bool VerifyHolder(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, HolderView* output) {
  const State* state = CurrentState();
  const InterfaceRuntime& runtime = *state->runtime;
  const auto reject_holder = [&](const char* reason) {
    Disable(reason);
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, reason);
  };
  if (!data->holder || !data->holder->array.reference ||
      !data->holder->array.Get<ObjectArray>() ||
      data->holder->array.Get<ObjectArray>()->length != state->batch_size * RedefinitionDataIter::kNumSlots)
    return reject_holder("interface holder array declaration mismatch");
  auto* definitions = Field<void**>(data->holder, runtime.redefinitions);
  if (!definitions || !definitions[0] || data->index < 0 || data->index >= state->batch_size ||
      reinterpret_cast<uintptr_t>(definitions[1]) != reinterpret_cast<uintptr_t>(definitions[0]) +
          state->batch_size * runtime.redefinition_size ||
      reinterpret_cast<uintptr_t>(definitions[2]) < reinterpret_cast<uintptr_t>(definitions[1]) ||
      reinterpret_cast<uintptr_t>(self) != reinterpret_cast<uintptr_t>(definitions[0]) +
          data->index * runtime.redefinition_size ||
      Slot(*data, 4) != process->mirror_class(self))
    return reject_holder("interface holder redefinitions declaration mismatch");
  auto& initialized = Field<std::vector<bool>>(data->holder, runtime.initialized);
  auto& structural = Field<std::vector<bool>>(data->holder, runtime.actually_structural);
  auto& initial = Field<std::vector<bool>>(data->holder, runtime.initial_structural);
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
    if (Slot(item, 4) != process->mirror_class(context) || !cache ||
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
  State* state = CurrentState();
  const InterfaceRuntime& runtime = *state->runtime;
  Attempt* attempt = state->attempt;
  if (!attempt || attempt->driver != self->driver)
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface redefinition requires private array roots");
  const auto reject_holder = [&](const char* reason) {
    Disable(reason);
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, reason);
  };
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(15)) return reject_holder("interface holder probe rejected injected mismatch");
)
  auto& initialized = *holder.initialized;
  auto& structural = *holder.structural;
  auto& initial = *holder.initial;
  if (initialized[data->index] || !structural[data->index] || !initial[data->index])
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface target requires an unallocated structural transaction");
  process->verified_release.store(state->selected_release);
  const Target* target = Find(process->mirror_class(self));
  if (!target) return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot target unavailable");
  if (!target->arrays)
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface private arrays were not prepared while Native");
  const auto& ordered = target->snapshot.types;
  if (ordered.empty() || Decode(ordered.front()) != process->mirror_class(self))
    return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot target is missing or not first");
  for (jclass klass : ordered)
    if (Status(Decode(klass)) < ClassStatus::kResolved)
      return FailAllocation(self, JVMTI_ERROR_INVALID_CLASS, "interface snapshot contains an unresolved class");
  constexpr int32_t kExtraRoots = 6;
  if (ordered.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max() - kExtraRoots))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition has too many types to root");
  void* thread = self->driver->self;
  if (thread != runtime_thread::Current(Thread()))
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface transaction thread mismatch");
  Roots roots(thread, ordered.size() + kExtraRoots);
  std::vector<Handle> old_types;
  for (jclass klass : ordered) old_types.push_back(roots.Add(Decode(klass)));
  Handle old_array = roots.Add(target->arrays->Old(process->decode_global));
  Handle new_array = roots.Add(target->arrays->New(process->decode_global));
  if (!old_array.Get<void>() || !new_array.Get<void>())
    return reject_holder("interface private array global reference decode failed");
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
    void* super = runtime_thread::Read(Thread(), Field<uint32_t>(old, runtime.super_class));
    const int32_t parent = target->snapshot.superclass[i];
    if (parent >= 0) {
      if (static_cast<size_t>(parent) >= i || old_types[parent].Get<void>() != super)
        return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface snapshot superclass changed");
      super = runtime_thread::Read(Thread(), new_array.Get<ObjectArray>()->elements[parent]);
    }
    Assign(superclass, super);
    Assign(original_cache, runtime_thread::Read(Thread(), Field<uint32_t>(old, runtime.dex_cache)));
    const int32_t batch_index = batch_indices[i];
    RedefinitionDataIter item{batch_index, data->holder};
    auto* allocator = batch_index >= 0 ? holder.Definition(batch_index) : self;
    if (batch_index >= 0) {
      Assign(cache, Slot(item, 3));  // Incoming DEX, rooted by ART's holder.
    } else {
      const auto* dex = reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
      Assign(loader, runtime_thread::Read(Thread(), Field<uint32_t>(old, runtime.class_loader)));
      // CreateNewDexCache reads only driver_ and dex_file_. This non-owning
      // source prefix does not swap or destroy ART's owning DexFile pointer.
      ClassRedefinitionPrefix context{self->driver, nullptr, dex};
      Assign(cache, process->allocate_cache(&context, loader));
    }
    if (!cache.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition private DexCache allocation failed");
    const DexFilePrefix* expected_dex = batch_index >= 0 ? allocator->dex_file
        : reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
    if (cache.Get<DexCache>()->dex_file != reinterpret_cast<uintptr_t>(expected_dex) ||
        runtime_thread::Read(Thread(), cache.Get<DexCache>()->class_loader) != runtime_thread::Read(Thread(), Field<uint32_t>(old_types[i].Get<void>(), runtime.class_loader)) ||
        cache.Get<void>() == original_cache.Get<void>())
      return reject_holder("interface private DexCache declaration mismatch");
    const Linking scope{&old_types, new_array, cache, static_cast<int32_t>(i)};
    void* replacement;
    {
      LinkingScope active(state, &scope);
      replacement = process->allocate_class(allocator, old_types[i], superclass, cache,
          batch_index >= 0 ? 0 : static_cast<uint16_t>(Field<int32_t>(old_types[i].Get<void>(), runtime.dex_class_def_index)));
    }
    if (attempt->error != JVMTI_ERROR_NONE)
      return FailAllocation(self, attempt->error, "interface linking hook failed");
    if (!replacement) {
      // ART 13 may fail before any usable candidate IfTable exists. Convert
      // only its exact selected non-public pair for a pending obligation.
      const auto& failure = attempt->non_public;
      if (failure.index == static_cast<int32_t>(i) && interface_dispatch::DescribeNonPublicSelection(
              Thread(), self, old_types, new_array, static_cast<int32_t>(i), failure.selection,
              target->snapshot.descriptors[i], batch_index >= 0, &transaction::Current()->rejection))
        self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
      return false;  // ART recorded its error and cleared the Java exception.
    }
    if (batch_index < 0) process->set_cache(replacement, original_cache.Get<void>());
ARTDCEVM_TEST(
    // Transfer failure after linking, now outside the callback and its locks.
    if (ARTDCEVM_FAULT(12)) throw JniFailure{};
)
    Store(new_array.Get<ObjectArray>(), static_cast<int32_t>(i), replacement);
ARTDCEVM_TEST(
    fault_injection::Increment(fault_injection::Counter::kAllocatedTypes);
)
    Assign(attempt->pending, nullptr);
    if (batch_index >= 0) {
      SetSlot(item, RedefinitionDataIter::kSlotNewClassObject, replacement);
      initialized[batch_index] = true;
    }
  }
  SetSlot(*data, RedefinitionDataIter::kSlotOldClasses, old_array.Get<void>());
  SetSlot(*data, RedefinitionDataIter::kSlotNewClasses, new_array.Get<void>());
  initialized[data->index] = true;
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(2)) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after interface allocation");
  if (fault_injection::Increment(fault_injection::Counter::kAllocatedGroups) == 2 && ARTDCEVM_FAULT(18))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after multiple interface groups allocation");
)
  // Run owns cleanup for every subsequent failure, including verification,
  // instance allocation, mapping probes and the subclass-load retry guard.
  return true;
}
const Target* AffectedTarget(void* klass) {
  const State* state = CurrentState();
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
    const Target* target = process->backend->Available() ? AffectedTarget(process->mirror_class(self)) : nullptr;
    if (!target) return reinterpret_cast<Function>(process->original_finish)(self, storage, data);
    HolderView holder;
    if (!VerifyHolder(self, data, &holder)) return false;
    if ((*holder.initialized)[data->index]) return true;
    // ART sorts by superclass depth, not interface dependencies. The first
    // affected entry delegates the complete group to its interface owner.
    for (jint i = 0; i < CurrentState()->batch_size; ++i) {
      auto* owner = holder.Definition(i);
      if (owner->klass != target->klass) continue;
ARTDCEVM_TEST(
      fault_injection::Set(fault_injection::Counter::kAllocationOrder, data->index < i ? 1 : 0);
)
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

}  // namespace detail
void RuntimeBackend::Discard() { detail::Discard(*CurrentState()->attempt); }
bool RuntimeBackend::Check(const runtime_thread::VerifiedThread& thread, ClassRedefinitionPrefix* self,
                    const RedefinitionDataIter& data, std::string* rejection) {
  return interface_dispatch::Check(thread, self, data, rejection);
}

}  // namespace artdcevm::interface_redefinition

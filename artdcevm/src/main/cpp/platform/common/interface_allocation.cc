#include "platform/common/interface_redefinition_internal.h"
#include "platform/common/art_method_identity.h"
#include "platform/common/interface_method_selection.h"
#include "platform/common/batch_allocation_order.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
namespace detail {
// Match HandleScope::Create/Thread::PushHandleScope rather than borrowing
// unrooted raw pointers across ART allocations. No Java allocation occurs while
// gathering the Class objects or constructing this C++ storage.
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
Handle ReplacementRoot(const Group& group) {
  const State* state = CurrentState();
  return state->attempt->replacements[&group - state->groups.data()];
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
  ~AttemptScope() { Discard(*state_->attempt); state_->attempt = previous_; }
 private:
  State* state_;
  Attempt* previous_;
};
jvmtiError Run(RedefinerPrefix* driver) noexcept {
  using Function = jvmtiError (*)(RedefinerPrefix*);
  State* state = CurrentState();
  if (!state || state->groups.empty())
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
    Roots roots(driver->self, state->groups.size() + 1);
    Attempt next{driver, {}, roots.Add(nullptr), false, JVMTI_ERROR_NONE, {}};
    for (size_t i = 0; i < state->groups.size(); ++i) next.replacements.push_back(roots.Add(nullptr));
    AttemptScope scope(state, &next);
    const jvmtiError result = reinterpret_cast<Function>(process->original_run)(driver);
    next.committed = result == JVMTI_ERROR_NONE;
    interface_method_selection::Complete(next.committed);
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
    state->attempt->non_public = {implementation, declaration, state->linking->index};
  using Function = void (*)(void*, ArtMethod*, ArtMethod*);
  reinterpret_cast<Function>(process->original_non_public)(klass, implementation, declaration);
}

using DescribeNonPublicFn = bool (*)(const runtime_thread::VerifiedThread&, const NonPublicFailure&,
                                     const std::string&, const std::string&, std::string*);
template <typename Class, typename Method> struct NonPublicDescriber {
  static bool Describe(const runtime_thread::VerifiedThread& thread, const NonPublicFailure& failure,
                       const std::string& target, const std::string& concrete, std::string* rejection) {
    auto* implementation = reinterpret_cast<Method*>(failure.implementation);
    auto* declaration = reinterpret_cast<Method*>(failure.declaration);
    auto* owner = implementation ? class_layout::Heap<Class>(thread, implementation->declaring_class) : nullptr;
    auto* interface = declaration ? class_layout::Heap<Class>(thread, declaration->declaring_class) : nullptr;
    evolution::Member method;
    std::string why;
    if (!owner || !interface || !class_layout::MethodIdentity<Class>(thread, declaration, &method, &why)) return false;
    *rejection = target + ": " + concrete + " links non-public implementation " + class_layout::ClassDescriptor(thread, *owner) +
        "." + method.name + method.descriptor + " of " + class_layout::ClassDescriptor(thread, *interface) +
        " as IllegalAccessError";
    return true;
  }
  static constexpr DescribeNonPublicFn Make(int) { return &Describe; }
};
const auto kNonPublicDescribers = class_layout::Table<NonPublicDescriber, DescribeNonPublicFn>();
bool DescribeNonPublic(const runtime_thread::VerifiedThread& thread, ClassRedefinitionPrefix* self, const NonPublicFailure& failure,
                       const std::string& concrete, std::string* rejection) {
  const int selected = class_layout::Selected();
  const Target* target = Find(process->mirror_class(self));
  return selected >= 0 && target &&
      kNonPublicDescribers[selected](thread, failure, target->live.descriptor, concrete, rejection);
}

bool LinkClass(void* linker, void* thread, const char* descriptor, Handle klass,
               Handle interfaces, Handle* output) noexcept {
  using Function = bool (*)(void*, void*, const char*, Handle, Handle, Handle*);
  State* state = CurrentState();
  const Linking* linking = state ? state->linking : nullptr;
  if (linking && !descriptor && !interface_method_selection::LinkingHelper()) Assign(state->attempt->pending, klass.Get<void>());
  const bool result = interface_method_selection::LinkClass(reinterpret_cast<Function>(process->original_link_class),
      linker, thread, descriptor, klass, interfaces, output);
  // Capture the resolved class before AllocateNewClassObject's JNI-ID array
  // allocations. Those can fail after linking and return nullptr to Finish.
  // This callback may run with ClassLinker locks held. Only assign an existing
  // HandleScope root: no JNI/local-table growth, heap store or allocation here.
  if (linking && !descriptor && output && output->Get<void>() && !interface_method_selection::LinkingHelper())
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
// The replacement an allocated rebuild holds for `old`. `pending` is set when
// `old` belongs to a rebuild whose replacement is not allocated yet.
void* ReplacementFor(void* old, bool* pending) {
  const State* state = CurrentState();
  if (!state || !state->attempt) return nullptr;
  for (const auto& group : state->groups) {
    const TypeSnapshot* allocated = group.Allocated();
    auto* replacements = allocated ? ReplacementRoot(group).Get<ObjectArray>() : nullptr;
    for (size_t i = 0; replacements && i < allocated->types.size(); ++i) {
      if (Decode(allocated->types[i]) != old) continue;
      void* replacement = runtime_thread::Read(Thread(), replacements->elements[i]);
      if (!replacement && pending) *pending = true;
      return replacement;
    }
  }
  return nullptr;
}
void* Redirect(void* linker, TypeIndex index, void* cache, void* loader) noexcept try {
  State* state = CurrentState();
  const Linking* linking = state ? state->linking : nullptr;
  if (!state) return nullptr;
  if (state->selection_types && cache == state->selection_cache.Get<void>()) {
    return index.index < state->selection_types->size() ? (*state->selection_types)[index.index].Get<void>() : nullptr;
  }
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
  bool pending = false;
  void* replacement = nullptr;
  replacement = ReplacementFor(old, &pending);
  if (pending && state->attempt) state->attempt->error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  return replacement;
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
  if (const State* state = CurrentState(); state && (state->linking || state->selection_types)) {
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
  const InterfaceRuntime* runtime = nullptr;
  uint8_t* begin = nullptr;
  std::vector<bool>* initialized = nullptr;
  std::vector<bool>* structural = nullptr;
  std::vector<bool>* initial = nullptr;
  ClassRedefinitionPrefix* Definition(int32_t index) const {
    return reinterpret_cast<ClassRedefinitionPrefix*>(begin + index * runtime->redefinition_size);
  }
};
// ART's holder read with `runtime`'s fixed declarations, or the first
// declaration mismatch. Every element's prefix, Class and DexCache slots must
// name this batch's own redefinitions.
const char* ViewHolder(const InterfaceRuntime& runtime, ClassRedefinitionPrefix* self, RedefinitionDataIter* data,
                       HolderView* output) {
  const State* state = CurrentState();
  if (!data->holder || !data->holder->array.reference ||
      !data->holder->array.Get<ObjectArray>() ||
      data->holder->array.Get<ObjectArray>()->length != state->batch_size * RedefinitionDataIter::kNumSlots)
    return "interface holder array declaration mismatch";
  auto* definitions = Field<void**>(data->holder, runtime.redefinitions);
  if (!definitions || !definitions[0] || data->index < 0 || data->index >= state->batch_size ||
      reinterpret_cast<uintptr_t>(definitions[1]) != reinterpret_cast<uintptr_t>(definitions[0]) +
          state->batch_size * runtime.redefinition_size ||
      reinterpret_cast<uintptr_t>(definitions[2]) < reinterpret_cast<uintptr_t>(definitions[1]) ||
      reinterpret_cast<uintptr_t>(self) != reinterpret_cast<uintptr_t>(definitions[0]) +
          data->index * runtime.redefinition_size ||
      Slot(*data, 4) != process->mirror_class(self))
    return "interface holder redefinitions declaration mismatch";
  auto& initialized = Field<std::vector<bool>>(data->holder, runtime.initialized);
  auto& structural = Field<std::vector<bool>>(data->holder, runtime.actually_structural);
  auto& initial = Field<std::vector<bool>>(data->holder, runtime.initial_structural);
  if (initialized.size() != static_cast<size_t>(state->batch_size) ||
      structural.size() != initialized.size() || initial.size() != initialized.size())
    return "interface holder flag declarations mismatch";
  HolderView view{&runtime, static_cast<uint8_t*>(definitions[0]), &initialized, &structural, &initial};
  for (jint i = 0; i < state->batch_size; ++i) {
    auto* context = view.Definition(i);
    bool known = false;
    for (jint j = 0; j < state->batch_size; ++j)
      known |= context->klass == state->definitions[j].klass;
    // Never decode an unverified candidate jclass from a guessed element.
    if (!known || context->driver != self->driver)
      return "interface holder element prefix declaration mismatch";
    RedefinitionDataIter item{i, data->holder};
    auto* cache = static_cast<DexCache*>(Slot(item, 3));
    if (Slot(item, 4) != process->mirror_class(context) || !cache ||
        cache->dex_file != reinterpret_cast<uintptr_t>(context->dex_file))
      return "interface holder Class/DEX-cache slots declaration mismatch";
  }
  *output = view;
  return nullptr;
}
bool VerifyHolder(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, HolderView* output) {
  if (const char* reason = ViewHolder(*CurrentState()->runtime, self, data, output)) {
    Disable(reason);
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, reason);
  }
  return true;
}
// Ownership uses the old hierarchy, exactly as ART's instance scan does.
// Allocation uses the final hierarchy and can interleave those owners.
int32_t Entry(const HolderView& holder, jclass klass) {
  for (jint i = 0; i < CurrentState()->batch_size; ++i)
    if (process->mirror_class(holder.Definition(i)) == Decode(klass)) return i;
  return -1;
}
void ChooseMembers(Group& group, const HolderView& holder) {
  bool extended = false;
  for (jclass ancestor : group.ancestors) {
    const int32_t entry = Entry(holder, ancestor);
    extended |= entry >= 0 && (*holder.structural)[entry];
  }
  group.uses_extended = extended;
  const auto& orderings = group.Current().orderings;
  for (size_t i = 0; group.owner < 0 && i < orderings.size(); ++i) {
    const int32_t entry = Entry(holder, orderings[i].owner);
    if (entry >= 0 && (*holder.structural)[entry] && (*holder.initial)[entry])
      group.owner = static_cast<int32_t>(i);
  }
}
const Group* AffectedGroup(void* klass) {
  const State* state = CurrentState();
  if (!state) return nullptr;
  for (const auto& group : state->groups) {
    if (group.class_root && group.owner < 0) continue;
    for (jclass type : group.Current().snapshot.types)
      if (Decode(type) == klass) return &group;
  }
  return nullptr;
}

bool AllocateBatch(ClassRedefinitionPrefix* self, RedefinitionDataIter* data, const HolderView& holder) {
  State* state = CurrentState();
  Attempt* attempt = state->attempt;
  const InterfaceRuntime& runtime = Runtime();
  // First freeze interface membership; structural ordinary ancestors are
  // absorbed before selecting any remaining ordinary initial roots.
  for (auto& group : state->groups)
    if (!group.class_root) ChooseMembers(group, holder);
  for (auto& group : state->groups) {
    if (!group.class_root) continue;
    const int32_t entry = Entry(holder, group.class_root);
    if (entry < 0 || !(*holder.structural)[entry] || !(*holder.initial)[entry]) continue;
    bool covered = false;
    for (const auto& other : state->groups) {
      if (other.class_root || other.owner < 0) continue;
      for (jclass type : other.Allocated()->types)
        covered |= Decode(type) == Decode(group.class_root);
    }
    if (!covered) ChooseMembers(group, holder);
  }
  struct Node { size_t group, slot; int32_t entry; Handle old, parent; };
  size_t count = 0;
  for (const auto& group : state->groups)
    if (const auto* types = group.Allocated()) count += types->types.size();
  if (count > static_cast<size_t>(std::numeric_limits<int32_t>::max() / 2 - 8))
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "batch has too many replacement classes");
  Roots roots(self->driver->self, 2 * count + 2 * state->groups.size() + 4);
  std::vector<Node> nodes;
  std::vector<Handle> old_arrays(state->groups.size()), new_arrays(state->groups.size());
  std::vector<int32_t> owners(state->groups.size(), -1);
  for (size_t g = 0; g < state->groups.size(); ++g) {
    auto& group = state->groups[g];
    const auto* snapshot = group.Allocated();
    if (!snapshot) continue;
    owners[g] = Entry(holder, group.Current().orderings[group.owner].owner);
    if (owners[g] < 0 || !group.Current().arrays)
      return FailAllocation(self, JVMTI_ERROR_INTERNAL, "batch rebuild owner or private arrays unavailable");
    old_arrays[g] = roots.Add(group.Current().arrays->Old(process->decode_global));
    new_arrays[g] = roots.Add(group.Current().arrays->New(process->decode_global));
    if (!old_arrays[g].Get<void>() || !new_arrays[g].Get<void>() ||
        old_arrays[g].Get<ObjectArray>()->length != static_cast<int32_t>(snapshot->types.size()) ||
        new_arrays[g].Get<ObjectArray>()->length != static_cast<int32_t>(snapshot->types.size()))
      return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "batch private array declaration mismatch");
    Assign(ReplacementRoot(group), new_arrays[g].Get<void>());
    RedefinitionDataIter owner{owners[g], data->holder};
    SetSlot(owner, RedefinitionDataIter::kSlotOldClasses, old_arrays[g].Get<void>());
    SetSlot(owner, RedefinitionDataIter::kSlotNewClasses, new_arrays[g].Get<void>());
    for (size_t i = 0; i < snapshot->types.size(); ++i) {
      const auto type = snapshot->types[i];
      Handle old = roots.Add(Decode(type));
      if (Status(old.Get<void>()) < ClassStatus::kResolved)
        return FailAllocation(self, JVMTI_ERROR_INVALID_CLASS, "batch contains an unresolved replacement class");
      for (const auto& node : nodes)
        if (node.old.Get<void>() == old.Get<void>())
          return FailAllocation(self, JVMTI_ERROR_INTERNAL, "batch replacement class has multiple migration owners");
      const int32_t entry = Entry(holder, type);
      void* parent = runtime_thread::Read(Thread(), Field<uint32_t>(old.Get<void>(), runtime.super_class));
      const auto* requirements = entry >= 0 ? transaction::Current()->Find(holder.Definition(entry)) : nullptr;
      if (requirements && requirements->superclass) parent = Decode(requirements->superclass);
      nodes.push_back({g, i, entry, old, roots.Add(parent)});
      Store(old_arrays[g].Get<ObjectArray>(), static_cast<int32_t>(i), old.Get<void>());
      if (entry >= 0) {
        (*holder.structural)[entry] = true;
        (*holder.initial)[entry] = entry == owners[g];
      }
    }
  }
  // Resolve direct interface identities through each class's own loader and
  // final DEX. Only named LookupClass services read the published class table;
  // no old DexCache is populated. All identities become roots before allocation.
  std::vector<std::vector<size_t>> dependencies(nodes.size());
  const auto depend = [&](size_t node, void* parent) {
    for (size_t j = 0; j < nodes.size(); ++j)
      if (nodes[j].old.Get<void>() == parent) dependencies[node].push_back(j);
  };
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    depend(i, node.parent.Get<void>());
    if (node.entry >= 0) {
      if (const auto* requirements = transaction::Current()->Find(holder.Definition(node.entry))) {
        for (jclass chain : requirements->chain_rebuilds) depend(i, Decode(chain));
        for (jclass interface : requirements->linked_interfaces) depend(i, Decode(interface));
      }
    }
    auto* old_cache = static_cast<DexCache*>(runtime_thread::Read(Thread(), Field<uint32_t>(node.old.Get<void>(), runtime.dex_cache)));
    const auto* dex = node.entry >= 0 ? holder.Definition(node.entry)->dex_file
        : reinterpret_cast<const DexFilePrefix*>(old_cache->dex_file);
    const uint32_t class_def = node.entry >= 0 ? 0 : Field<int32_t>(node.old.Get<void>(), runtime.dex_class_def_index);
    std::vector<std::string> interfaces;
    std::string error;
    if (!dex || !evolution::ReadClassInterfaces(DexViewOf(*dex), class_def, &interfaces, &error))
      return FailAllocation(self, JVMTI_ERROR_INVALID_CLASS_FORMAT, "batch final interface declarations unavailable");
    void* loader = runtime_thread::Read(Thread(), Field<uint32_t>(node.old.Get<void>(), runtime.class_loader));
    for (const auto& descriptor : interfaces) {
      void* interface = process->lookup_view ? process->lookup_view(process->class_linker.load(), runtime_thread::Current(Thread()), descriptor, loader)
          : process->lookup_chars(process->class_linker.load(), runtime_thread::Current(Thread()), descriptor.c_str(), loader);
      if (!interface)
        return FailAllocation(self, JVMTI_ERROR_FAILS_VERIFICATION, "batch final interface is not loaded");
      depend(i, interface);
    }
  }
  std::vector<size_t> order;
  if (!batch_allocation::Order(dependencies, &order))
    return FailAllocation(self, JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION, "batch final replacement hierarchy is cyclic");
  Handle cache = roots.Add(nullptr), original_cache = roots.Add(nullptr), superclass = roots.Add(nullptr), loader = roots.Add(nullptr);
  for (size_t position : order) {
    const auto& node = nodes[position];
    auto& group = state->groups[node.group];
    RedefinitionDataIter item{node.entry, data->holder};
    auto* allocator = node.entry >= 0 ? holder.Definition(node.entry) : holder.Definition(owners[node.group]);
    void* parent = node.parent.Get<void>();
    bool pending = false;
    if (void* replacement = ReplacementFor(parent, &pending)) parent = replacement;
    if (pending) return FailAllocation(self, JVMTI_ERROR_INTERNAL, "batch superclass dependency was not allocated");
    Assign(superclass, parent);
    Assign(original_cache, runtime_thread::Read(Thread(), Field<uint32_t>(node.old.Get<void>(), runtime.dex_cache)));
    if (node.entry >= 0) {
      Assign(cache, Slot(item, 3));
    } else {
      const auto* dex = reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
      Assign(loader, runtime_thread::Read(Thread(), Field<uint32_t>(node.old.Get<void>(), runtime.class_loader)));
      ClassRedefinitionPrefix context{self->driver, nullptr, dex};
      Assign(cache, process->allocate_cache(&context, loader));
    }
    if (!cache.Get<void>()) return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "batch private DexCache allocation failed");
    const auto* expected_dex = node.entry >= 0 ? allocator->dex_file
        : reinterpret_cast<const DexFilePrefix*>(original_cache.Get<DexCache>()->dex_file);
    if (cache.Get<DexCache>()->dex_file != reinterpret_cast<uintptr_t>(expected_dex) ||
        runtime_thread::Read(Thread(), cache.Get<DexCache>()->class_loader) !=
            runtime_thread::Read(Thread(), Field<uint32_t>(node.old.Get<void>(), runtime.class_loader)) ||
        cache.Get<void>() == original_cache.Get<void>())
      return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "batch private DexCache declaration mismatch");
    const Linking scope{cache, static_cast<int32_t>(node.slot)};
    void* replacement;
    {
      LinkingScope active(state, &scope);
      replacement = process->allocate_class(allocator, node.old, superclass, cache,
          node.entry >= 0 ? 0 : static_cast<uint16_t>(Field<int32_t>(node.old.Get<void>(), runtime.dex_class_def_index)));
    }
    if (attempt->error != JVMTI_ERROR_NONE) return FailAllocation(self, attempt->error, "batch linking hook failed");
    if (!replacement) {
      const auto& failure = attempt->non_public;
      if (failure.index == static_cast<int32_t>(node.slot) &&
          DescribeNonPublic(Thread(), allocator, failure, group.Allocated()->descriptors[node.slot], &transaction::Current()->rejection))
        self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
      return false;
    }
    if (node.entry < 0) process->set_cache(replacement, original_cache.Get<void>());
ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT(12)) throw JniFailure{};
)
    Store(new_arrays[node.group].Get<ObjectArray>(), static_cast<int32_t>(node.slot), replacement);
ARTDCEVM_TEST(
    fault_injection::Increment(fault_injection::Counter::kAllocatedTypes);
)
    Assign(attempt->pending, nullptr);
    if (node.entry >= 0) {
      SetSlot(item, RedefinitionDataIter::kSlotNewClassObject, replacement);
      (*holder.initialized)[node.entry] = true;
    }
  }
  for (size_t g = 0; g < owners.size(); ++g) {
    if (owners[g] < 0) continue;
    RedefinitionDataIter owner{owners[g], data->holder};
    SetSlot(owner, RedefinitionDataIter::kSlotOldClasses, old_arrays[g].Get<void>());
    SetSlot(owner, RedefinitionDataIter::kSlotNewClasses, new_arrays[g].Get<void>());
ARTDCEVM_TEST(
    if (!state->groups[g].class_root) {
      if (ARTDCEVM_FAULT(2)) return FailAllocation(holder.Definition(owners[g]), JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after interface allocation");
      if (fault_injection::Increment(fault_injection::Counter::kAllocatedGroups) == 2 && ARTDCEVM_FAULT(18))
        return FailAllocation(holder.Definition(owners[g]), JVMTI_ERROR_OUT_OF_MEMORY, "injected failure after multiple interface groups allocation");
    }
)
  }
  attempt->allocated = true;
  return true;
}

bool Finish(ClassRedefinitionPrefix* self, RedefinitionDataHolderPrefix& storage,
            RedefinitionDataIter* data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataHolderPrefix&, RedefinitionDataIter*);
  try {
    State* state = CurrentState();
    auto* context = transaction::Current();
    if (context) context->holder = data->holder;
    if (!state || !state->attempt) return reinterpret_cast<Function>(process->original_finish)(self, storage, data);
    HolderView holder;
    if (!VerifyHolder(self, data, &holder)) return false;
ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT(15)) {
      Disable("interface holder probe rejected injected mismatch");
      return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface holder probe rejected injected mismatch");
    }
)
    if (!state->attempt->allocated) {
      const auto start = std::chrono::steady_clock::now();
      if (!AllocateBatch(self, data, holder)) return false;
      timing.allocation_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
      process->verified_release.store(state->selected_release);
ARTDCEVM_TEST(
      for (const auto& group : state->groups)
        if (!group.class_root && group.owner >= 0) {
          const int32_t owner = Entry(holder, group.Current().orderings[group.owner].owner);
          if (data->index < owner) fault_injection::Set(fault_injection::Counter::kAllocationOrder, 1);
        }
)
    }
    if ((*holder.initialized)[data->index]) return true;
    return reinterpret_cast<Function>(process->original_finish)(self, storage, data);
  } catch (const std::bad_alloc&) {
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface redefinition native allocation failed");
  } catch (...) {
    return FailAllocation(self, ArtHookExceptionError(), "interface allocation hook failed");
  }
}

// ART's CollectAndCreateNewInstances collects instances of the owner entry's
// class and allocates their replacements. For each further root of the
// rebuild, run it again with that root as the entry's class, then keep each
// instance from the round of the first root its class is assignable to, so
// every instance is migrated once. Only ART allocates the replacements; the
// merged arrays are allocated through public JNI on this Runnable thread.
bool CollectInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data,
                      bool (*collect)(ClassRedefinitionPrefix*, RedefinitionDataIter*)) {
  State* state = CurrentState();
  const Ordering* ordering = nullptr;
  if (state && state->attempt)
    for (const auto& group : state->groups)
      if (group.owner >= 0 && group.Current().orderings[group.owner].owner == self->klass)
        ordering = &group.Current().orderings[group.owner];
  if (!ordering || !ordering->class_roots) {
    if (!collect(self, data)) return false;  // ART recorded its error.
    if (!ordering || ordering->rounds.empty()) return true;
  }
  if (!state->object_class)
    return FailAllocation(self, JVMTI_ERROR_NOT_AVAILABLE, "interface instance merge JNI services unavailable");
  using Slots = RedefinitionDataIter;
  const auto& types = ordering->snapshot.types;
  Roots roots(runtime_thread::Current(Thread()), types.size() + 1 + 2 * (ordering->rounds.size() + 1));
  std::vector<Handle> classes;
  for (jclass type : types) classes.push_back(roots.Add(Decode(type)));
  const Handle owner = roots.Add(Slot(*data, Slots::kSlotMirrorClass));
  std::vector<std::pair<int32_t, std::pair<Handle, Handle>>> results;
  if (!ordering->class_roots)
    results.push_back({0, {roots.Add(Slot(*data, Slots::kSlotOldInstanceObjects)),
                           roots.Add(Slot(*data, Slots::kSlotNewInstanceObjects))}});
  for (int32_t round : ordering->rounds) {
    // Roots are batch references; decode by identity, not by JNI reference.
    SetSlot(*data, Slots::kSlotMirrorClass, Decode(ordering->roots[round]));
    const bool collected = collect(self, data);
    SetSlot(*data, Slots::kSlotMirrorClass, owner.Get<void>());
    if (!collected) return false;  // ART recorded its error.
    results.push_back({round, {roots.Add(Slot(*data, Slots::kSlotOldInstanceObjects)),
                               roots.Add(Slot(*data, Slots::kSlotNewInstanceObjects))}});
  }
  // Class pointers are read again after the rounds' allocations.
  std::unordered_map<void*, int32_t> first_root;
  for (size_t i = 0; i < types.size(); ++i) first_root[classes[i].Get<void>()] = ordering->first_root[i];
  const auto kept = [&](int32_t round, ObjectArray* olds, int32_t i) {
    void* instance = runtime_thread::Read(Thread(), olds->elements[i]);
    const auto found = first_root.find(runtime_thread::Read(Thread(), static_cast<Object*>(instance)->klass));
    return found != first_root.end() && found->second == round;
  };
  int32_t total = 0;
  for (const auto& [round, arrays] : results)
    for (int32_t i = 0; i < arrays.first.Get<ObjectArray>()->length; ++i) total += kept(round, arrays.first.Get<ObjectArray>(), i);
  JNIEnv* jni = state->jni;
  jobjectArray merged[2] = {jni->NewObjectArray(total, state->object_class, nullptr),
                            jni->NewObjectArray(total, state->object_class, nullptr)};
  if (!merged[0] || !merged[1]) {
    jni->ExceptionClear();
    for (jobjectArray array : merged) if (array) jni->DeleteLocalRef(array);
    return FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface instance merge allocation failed");
  }
  int32_t position = 0;
  for (const auto& [round, arrays] : results) {
    for (int32_t i = 0; i < arrays.first.Get<ObjectArray>()->length; ++i) {
      if (!kept(round, arrays.first.Get<ObjectArray>(), i)) continue;
      for (int side = 0; side < 2; ++side) {
        const Handle source = side ? arrays.second : arrays.first;
        LocalRef element(runtime_thread::Read(Thread(), source.Get<ObjectArray>()->elements[i]));
        jni->SetObjectArrayElement(merged[side], position, element.Get());
      }
      ++position;
    }
  }
  const bool stored = !jni->ExceptionCheck();
  jni->ExceptionClear();
  // The holder array is rooted by ART; publish the merged arrays into it.
  LocalRef holder(data->holder->array.Get<void>());
  for (int side = 0; stored && side < 2; ++side)
    jni->SetObjectArrayElement(static_cast<jobjectArray>(holder.Get()),
        Slots::kNumSlots * data->index + (side ? Slots::kSlotNewInstanceObjects : Slots::kSlotOldInstanceObjects),
        merged[side]);
  const bool published = stored && !jni->ExceptionCheck();
  jni->ExceptionClear();
  for (jobjectArray array : merged) jni->DeleteLocalRef(array);
  if (!published) return FailAllocation(self, JVMTI_ERROR_INTERNAL, "interface instance merge transfer failed");
  return true;
}
}  // namespace detail
bool RuntimeBackend::CompleteInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data,
                                       bool (*collect)(ClassRedefinitionPrefix*, RedefinitionDataIter*)) noexcept try {
  return RuntimeAvailable() ? detail::CollectInstances(self, data, collect) : collect(self, data);
} catch (const std::bad_alloc&) {
  return detail::FailAllocation(self, JVMTI_ERROR_OUT_OF_MEMORY, "interface instance merge native allocation failed");
} catch (...) {
  return detail::FailAllocation(self, ArtHookExceptionError(), "interface instance merge failed");
}

}  // namespace artdcevm::interface_redefinition

#include "platform/common/verification_redirect.h"
#include "platform/common/invoke_kind.h"
#include "platform/common/interface_redefinition.h"
#include "evolution/modifiers.h"

#include <android/log.h>
#include <atomic>
#include <new>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/android13/verifier.h"
#include "platform/abi/arm64/android16/verifier.h"
#include "platform/common/class_layout.h"
#include "platform/common/runtime_thread.h"
#include "platform/common/runtime_roots.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/verification_redirect_symbols.h"

namespace artdcevm::verification_redirect {
// The redefinition ART is verifying on this transaction thread.
struct Verification {
  const abi::arm64::RedefinitionDataIter* data;
  std::string_view descriptor;  // "Lpkg/Name;"
  // The backend that answers lookups for interface groups, or null.
  interface_redefinition::Backend* interface_group;
  // The class belongs to a group, whose arrays also hold its replacement.
  bool group_member;
};

namespace {
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::Handle;
using abi::arm64::RedefinitionDataIter;

struct RegisterInfo {
  void* cache;
  int rank;
  bool resolved;
};
struct Resolution {
  std::string_view descriptor;
  size_t depth = 0;
  bool resolved = false;
};
struct State final : transaction::Part {
  const Verification* verifying = nullptr;
  bool register_error = false;
  // Native arena objects, not managed references. The queried RegTypes keep
  // their cache alive; associate both operands rather than guessing the last
  // cache used by this thread (nested verification can use another cache).
  std::unordered_map<const void*, RegisterInfo> register_caches;
  Resolution* resolving = nullptr;
  size_t lookup_depth = 0;
  size_t register_remaps = 0;
};
State* CurrentState() {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::verification);
}

void* original_check_verification = nullptr;
void* original_find_class = nullptr;
void* original_find_class_with_length = nullptr;
void* original_register_find_class = nullptr;
void* original_register_insert_class = nullptr;
void* original_register_from_class = nullptr;
void* original_legacy_assignable = nullptr;
void* original_modern_assignable = nullptr;
void* original_legacy_from = nullptr;
void* original_legacy_from_class = nullptr;
void* original_modern_from = nullptr;
void* original_legacy_construct = nullptr;
void* original_modern_construct = nullptr;
abi::arm64::android13::verifier::GetComponentType legacy_component = nullptr;
abi::arm64::android16::verifier::GetComponentType modern_component = nullptr;
bool resolves_register_types = false;
bool resolves_array_assignability = false;
bool resolves_classes = false;
// FindClass hooks read it on any thread; Install publishes it.
std::atomic<interface_redefinition::Backend*> interfaces{nullptr};

// ART verifies an enum's synthetic self-array builders as well as every other
// incoming method. With identical hierarchy and member declarations, changing
// only ACC_ENUM cannot change verifier access or assignability. Use ART's
// existing published self/array graph for that verification: redirecting only
// self would mix a new Target with the published Target[] component. The
// replacement remains rooted in NewClasses, and is restored before allocation
// or commit. This changes neither code verification nor any published Class.
class PublishedVerification {
 public:
  PublishedVerification(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data)
      : thread_(thread), data_(data), replacement_(data.NewClassObject()) {
    auto* array = data.holder->array.Get<abi::arm64::ObjectArray>();
    Store(array->elements[data.index * RedefinitionDataIter::kNumSlots + RedefinitionDataIter::kSlotMirrorClass]);
  }
  ~PublishedVerification() { Store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement_))); }
 private:
  void Store(uint32_t value) {
    auto* array = data_.holder->array.Get<abi::arm64::ObjectArray>();
    auto* slot = &array->elements[data_.index * RedefinitionDataIter::kNumSlots + RedefinitionDataIter::kSlotNewClassObject];
    *slot = value;
    // Fixed ThreadTlsPtrPrefix and CardTable (11--17), also used by Proxy
    // relinking. The Native Thread probe proves the exception-adjacent slot.
    static_assert(offsetof(abi::arm64::ThreadTlsPtrPrefix, exception) -
                  offsetof(abi::arm64::ThreadTlsPtrPrefix, card_table) == sizeof(void*));
    auto* cards = runtime_roots::Field<uint8_t*>(runtime_thread::Current(thread_),
        runtime_thread::Layout(thread_).exception - sizeof(void*));
    cards[reinterpret_cast<uintptr_t>(slot) >> 10] = 0x70;
  }
  const runtime_thread::VerifiedThread& thread_;
  const RedefinitionDataIter& data_;
  void* replacement_;
};

bool CheckVerification(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (!target || !target->redirect_verification)
    return reinterpret_cast<Function>(original_check_verification)(self, data);
  if (target->published_verification) {
    const auto thread = runtime_thread::Ready();
    auto* classes = data.Classes(false);
    if (!thread || !classes || classes->length < 1 || classes->elements[0] !=
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data.NewClassObject()))) {
      self->driver->result = JVMTI_ERROR_INTERNAL;
      return false;
    }
    PublishedVerification scope(*thread, data);
    return reinterpret_cast<Function>(original_check_verification)(self, data);
  }
  // The standard entry creates this state while Native. Without it the new
  // class cannot be shown to ART's verifier: fail rather than verify against
  // the old one. This noexcept hook does not allocate a diagnostic.
  State* state = CurrentState();
  if (!state) {
    __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "verification redirection state is missing");
    self->driver->result = JVMTI_ERROR_INTERNAL;
    return false;
  }
  interface_redefinition::Backend* backend = interfaces.load(std::memory_order_acquire);
  const bool member = backend && backend->IsAffected(self);
  const Verification scope{&data, target->descriptor,
                           member || (backend && !target->linked_interfaces.empty()) ? backend : nullptr, member};
  const Verification* previous = std::exchange(state->verifying, &scope);
  auto previous_caches = std::move(state->register_caches);
  Resolution* previous_resolution = std::exchange(state->resolving, nullptr);
  const size_t previous_depth = std::exchange(state->lookup_depth, 0);
  const size_t previous_remaps = std::exchange(state->register_remaps, 0);
  const bool previous_error = std::exchange(state->register_error, false);
  const bool verified = reinterpret_cast<Function>(original_check_verification)(self, data);
  state->verifying = previous;
  state->register_caches = std::move(previous_caches);
  state->resolving = previous_resolution;
  state->lookup_depth = previous_depth;
  state->register_remaps = previous_remaps;
  const bool register_error = std::exchange(state->register_error, previous_error);
  if (register_error) {
    self->driver->result = JVMTI_ERROR_INTERNAL;
    return false;
  }
  if (const auto error = (backend ? backend->VerificationError() : JVMTI_ERROR_NONE); error != JVMTI_ERROR_NONE) {
    self->driver->result = error;
    return false;
  }
  return verified;
}

void* VerifiedClass(void* linker, std::string_view descriptor, Handle loader,
                    const State* state = CurrentState()) {
  const Verification* verifying = state ? state->verifying : nullptr;
  if (!verifying) return nullptr;
  if (!descriptor.empty() && descriptor.front() == '[') return nullptr;
  // A class outside the interface groups resolves itself to its new class and
  // the rebuilt interfaces it links to their replacements.
  if (!verifying->group_member && descriptor == verifying->descriptor) return verifying->data->NewClassObject();
  if (verifying->interface_group)
    return verifying->interface_group->VerificationClass(linker, descriptor, loader);
  return nullptr;
}
// A replacement's self type and its already allocated subtypes must share
// one identity graph. Reuse ART's rooted old/new class arrays, including
// implicitly rebuilt subclasses such as tool-appended classes. A descriptor
// lookup alone would mix a replacement parent with an old child.
void* RebuiltClass(void* found, const State* state = CurrentState()) {
  const Verification* verifying = state ? state->verifying : nullptr;
  if (!found || !verifying || verifying->interface_group) return found;
  const auto thread = runtime_thread::Ready();
  if (thread)
    if (void* rebuilt = class_layout::Rebuilt(*thread, verifying->data->holder, found)) return rebuilt;
  return found;
}
// A published array keeps its identity through ART's reference replacement.
// During verification its component can therefore still name an old class.
// Translate that component at the verifier's register-type boundary, using
// the same rooted replacement graph as descriptor resolution. Nothing is
// published into the class table or a DexCache on this path.
void* RegisterClass(void* found, bool* precise = nullptr) {
  State* state = CurrentState();
  const Verification* verifying = state ? state->verifying : nullptr;
  const auto thread = verifying ? runtime_thread::Ready() : std::nullopt;
  if (found && thread)
    if (void* rebuilt = class_layout::Rebuilt(*thread, verifying->data->holder, found)) {
      ++state->register_remaps;
      if (precise) {
        uint32_t flags = 0;
        if (!class_layout::ClassAccess(*thread, rebuilt, &flags)) {
          state->register_error = true;
          return found;
        }
        // Only ordinary classes occur in ART's rebuild arrays. Arrays and
        // primitives are final but never mapped here; interfaces are imprecise.
        *precise = (flags & evolution::kAccFinal) != 0;
      }
      return rebuilt;
    }
  return found;
}
// Array Class objects are canonical, including arrays first loaded by the
// verifier. Resolve their entire component chain against the published graph;
// only the verifier's register types use replacements. Otherwise nested
// FindClass calls could publish an array of a transaction-private component.
class PublishedArrayLookup {
 public:
  explicit PublishedArrayLookup(bool array, State* state = CurrentState()) : state_(array ? state : nullptr),
      previous_(state_ ? std::exchange(state_->verifying, nullptr) : nullptr) {}
  ~PublishedArrayLookup() { if (state_) state_->verifying = previous_; }
 private:
  State* state_;
  const Verification* previous_;
};
int ArrayRank(std::string_view descriptor) {
  int rank = 0;
  for (char c : descriptor) { if (c != '[') break; ++rank; }
  return rank;
}
void ResetCache(void* cache) {
  State* state = CurrentState();
  if (!state || !state->verifying) return;
  // Method verification destroys its arena. A subsequent cache may reuse
  // both its address and RegType addresses; proofs cannot cross that lifetime.
  for (auto it = state->register_caches.begin(); it != state->register_caches.end();) {
    if (it->second.cache == cache) it = state->register_caches.erase(it);
    else ++it;
  }
}
void LegacyConstruct(void* cache, void* linker, bool load, void* allocator, bool suspend) noexcept {
  using Function = abi::arm64::android13::verifier::Construct;
  ResetCache(cache);
  reinterpret_cast<Function>(original_legacy_construct)(cache, linker, load, allocator, suspend);
}
void ModernConstruct(void* cache, void* thread, void* linker, void* pool, Handle loader,
                     const void* dex, bool load, bool suspend) noexcept {
  using Function = abi::arm64::android16::verifier::Construct;
  ResetCache(cache);
  reinterpret_cast<Function>(original_modern_construct)(cache, thread, linker, pool, loader, dex, load, suspend);
}
const void* RecordRegister(const void* cache, const void* type, int rank = -1,
                           bool resolved = true) noexcept {
  State* state = CurrentState();
  if (resolves_array_assignability && type && state && state->verifying) {
    try {
      auto [it, inserted] = state->register_caches.try_emplace(type,
          RegisterInfo{const_cast<void*>(cache), rank, resolved});
      if (!inserted) {
        auto& info = it->second;
        if (info.cache != cache) info = {const_cast<void*>(cache), rank, resolved};
        else {
          if (rank >= 0) info.rank = rank;
          info.resolved |= resolved;
        }
      }
    } catch (const std::bad_alloc&) { state->register_error = true; }
  }
  return type;
}
const void* RegisterFindClass(const void* cache, void* klass, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::FindClass;
  klass = RegisterClass(klass, &precise);
  return RecordRegister(cache, reinterpret_cast<Function>(original_register_find_class)(cache, klass, precise));
}
const void* RegisterInsertClass(void* cache, const std::string_view& descriptor, void* klass, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::InsertClass;
  klass = RegisterClass(klass, &precise);
  return RecordRegister(cache, reinterpret_cast<Function>(original_register_insert_class)(cache, descriptor, klass, precise),
                        ArrayRank(descriptor));
}
const void* RegisterFromClass(void* cache, void* klass) noexcept {
  using Function = abi::arm64::android16::verifier::FromClass;
  return RecordRegister(cache, reinterpret_cast<Function>(original_register_from_class)(cache, RegisterClass(klass)));
}
// A descriptor alone does not prove that From returned a resolved RegType.
// Observe its actual outer FindClass result; recursive component lookups and
// nested cache resolutions cannot supply that proof for another descriptor.
class ResolutionScope {
 public:
  explicit ResolutionScope(const char* descriptor) : state_(CurrentState()),
      resolution_{descriptor ? descriptor : ""},
      previous_(state_ ? std::exchange(state_->resolving, &resolution_) : nullptr) {}
  ~ResolutionScope() { if (state_) state_->resolving = previous_; }
  bool Resolved() const { return resolution_.resolved; }
 private:
  State* state_;
  Resolution resolution_;
  Resolution* previous_;
};
class ResolutionLookup {
 public:
  explicit ResolutionLookup(std::string_view descriptor, State* state = CurrentState()) : state_(state),
      resolution_(state_ ? state_->resolving : nullptr),
      owner_(resolution_ && resolution_->depth++ == 0 && resolution_->descriptor == descriptor) {
    if (state_) ++state_->lookup_depth;
  }
  ~ResolutionLookup() {
    if (resolution_) --resolution_->depth;
    if (state_) --state_->lookup_depth;
  }
  void* Result(void* found) {
    if (owner_) resolution_->resolved = found != nullptr;
    return found;
  }
 private:
  State* state_;
  Resolution* resolution_;
  bool owner_;
};
std::optional<void*> VerificationLoader(const runtime_thread::VerifiedThread& thread, const State& state) {
  void* loader;
  const auto& data = *state.verifying->data;
  if (!class_layout::ClassLoader(thread, runtime_thread::Read(thread,
      data.Reference(RedefinitionDataIter::kSlotNewClassObject)), &loader)) return std::nullopt;
  return loader;
}
const void* LegacyFrom(void* cache, void* loader, const char* descriptor, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::From;
  ResolutionScope scope(descriptor);
  const void* type = reinterpret_cast<Function>(original_legacy_from)(cache, loader, descriptor, precise);
  return RecordRegister(cache, type, descriptor ? ArrayRank(descriptor) : -1, scope.Resolved());
}
const void* LegacyFromClass(void* cache, const char* descriptor, void* klass, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::FromClass;
  klass = RegisterClass(klass, &precise);
  return RecordRegister(cache, reinterpret_cast<Function>(original_legacy_from_class)(cache, descriptor, klass, precise),
                        descriptor ? ArrayRank(descriptor) : -1);
}
const void* ModernFrom(void* cache, const char* descriptor) noexcept {
  using Function = abi::arm64::android16::verifier::From;
  ResolutionScope scope(descriptor);
  const void* type = reinterpret_cast<Function>(original_modern_from)(cache, descriptor);
  return RecordRegister(cache, type, descriptor ? ArrayRank(descriptor) : -1, scope.Resolved());
}
bool LegacyAssignable(const void* lhs, const void* rhs, bool strict, void* verifier) noexcept;
bool ModernAssignable(const void* verifier, const void* lhs, const void* rhs, bool strict) noexcept;
std::optional<bool> ArrayAssignment(const void* lhs, const void* rhs, void* verifier, bool modern) noexcept {
  State* state = CurrentState();
  if (!state || !state->verifying || state->lookup_depth) return std::nullopt;
  const auto left = state->register_caches.find(lhs);
  const auto right = state->register_caches.find(rhs);
  if (left == state->register_caches.end() || right == state->register_caches.end()) return std::nullopt;
  // Copy native metadata before component lookup can allocate or rehash it.
  const RegisterInfo l = left->second, r = right->second;
  if (l.cache != r.cache || l.rank <= 0 || r.rank <= 0 || !l.resolved || !r.resolved) return std::nullopt;
  const auto thread = modern ? std::nullopt : runtime_thread::Ready();
  if (!modern && !thread) return std::nullopt;
  const size_t remaps = state->register_remaps;
  // RegTypes and their caches own ART's roots. Re-read the rooted legacy
  // loader before each potentially suspending lookup; never retain a raw Class
  // or loader across it. Modern caches already own their loader handle.
  const auto component = [&](const void* type) -> const void* {
    if (modern) return modern_component(l.cache, type);
    const auto loader = VerificationLoader(*thread, *state);
    if (!loader) { state->register_error = true; return nullptr; }
    return legacy_component(l.cache, type, *loader);
  };
  const void* component_left = component(lhs);
  if (!component_left) return std::nullopt;
  RecordRegister(l.cache, component_left, l.rank - 1, false);
  const void* component_right = component(rhs);
  if (!component_right) return std::nullopt;
  RecordRegister(r.cache, component_right, r.rank - 1, false);
  // Native FromClass supplies resolved-type proof for components. Unresolved
  // and primitive cases retain ART's answer. Array covariance always uses
  // strict interface rules, including when the outer assignment is lenient.
  const bool result = modern ? ModernAssignable(verifier, component_left, component_right, true) :
      LegacyAssignable(component_left, component_right, true, verifier);
  return state->register_remaps != remaps ? std::optional<bool>(result) : std::nullopt;
}
bool LegacyAssignable(const void* lhs, const void* rhs, bool strict, void* verifier) noexcept {
  using Function = abi::arm64::android13::verifier::AssignableFrom;
  if (auto result = ArrayAssignment(lhs, rhs, verifier, false)) return *result;
  return reinterpret_cast<Function>(original_legacy_assignable)(lhs, rhs, strict, verifier);
}
bool ModernAssignable(const void* verifier, const void* lhs, const void* rhs, bool strict) noexcept {
  using Function = abi::arm64::android16::verifier::AssignableFrom;
  if (auto result = ArrayAssignment(lhs, rhs, const_cast<void*>(verifier), true)) return *result;
  return reinterpret_cast<Function>(original_modern_assignable)(verifier, lhs, rhs, strict);
}
void* FindClass(void* linker, void* thread, const char* descriptor, Handle loader) noexcept {
  invoke_kind::ObserveClassLinker(linker);
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  State* state = CurrentState();
  ResolutionLookup lookup(descriptor ? descriptor : "", state);
  if (void* klass = descriptor ? VerifiedClass(linker, descriptor, loader, state) : nullptr) return lookup.Result(klass);
  using Function = void* (*)(void*, void*, const char*, Handle);
  void* found;
  {
    PublishedArrayLookup scope(descriptor && descriptor[0] == '[', state);
    found = reinterpret_cast<Function>(original_find_class)(linker, thread, descriptor, loader);
  }
  return lookup.Result(RebuiltClass(found, state));
}
void* FindClassWithLength(void* linker, void* thread, const char* descriptor, size_t length,
                          Handle loader) noexcept {
  invoke_kind::ObserveClassLinker(linker);
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  State* state = CurrentState();
  ResolutionLookup lookup(descriptor ? std::string_view(descriptor, length) : std::string_view{}, state);
  if (void* klass = descriptor ? VerifiedClass(linker, {descriptor, length}, loader, state) : nullptr) return lookup.Result(klass);
  using Function = void* (*)(void*, void*, const char*, size_t, Handle);
  void* found;
  {
    PublishedArrayLookup scope(descriptor && descriptor[0] == '[', state);
    found = reinterpret_cast<Function>(original_find_class_with_length)(linker, thread, descriptor, length, loader);
  }
  return lookup.Result(RebuiltClass(found, state));
}
}  // namespace

void Begin() {
  transaction::Current()->verification = std::make_unique<State>();
}

bool Prepare(std::vector<Hook>* hooks, std::string* failure) {
  using symbols::structural::kCheckVerification;
  using namespace symbols::verification_redirect;
  resolves_classes = false;
  resolves_register_types = false;
  resolves_array_assignability = false;
  legacy_component = nullptr;
  modern_component = nullptr;
  void* check = symbols::Find(kCheckVerification);
  if (!check) {
    *failure = std::string("missing ") + kCheckVerification.name;
    return false;
  }
  hooks->push_back({check, HookAddress(CheckVerification), &original_check_verification,
                    kCheckVerification.name});
  if (void* find = symbols::Find(kFindClass)) {
    hooks->push_back({find, HookAddress(FindClass), &original_find_class, kFindClass.name});
    resolves_classes = true;
  }
  if (void* find = symbols::Find(kFindClassWithLength)) {
    hooks->push_back({find, HookAddress(FindClassWithLength), &original_find_class_with_length,
                      kFindClassWithLength.name});
    resolves_classes = true;
  }
  void* register_find = symbols::Find(kRegisterFindClass);
  void* register_insert = symbols::Find(kRegisterInsertClass);
  if (register_find && register_insert) {
    hooks->push_back({register_find, HookAddress(RegisterFindClass), &original_register_find_class, kRegisterFindClass.name});
    hooks->push_back({register_insert, HookAddress(RegisterInsertClass), &original_register_insert_class, kRegisterInsertClass.name});
    resolves_register_types = true;
  } else if (void* from = symbols::Find(kRegisterFromClass)) {
    hooks->push_back({from, HookAddress(RegisterFromClass), &original_register_from_class, kRegisterFromClass.name});
    resolves_register_types = true;
  }
  void* assignable = symbols::Find(register_find && register_insert ? kLegacyAssignable : kModernAssignable);
  void* component = symbols::Find(register_find && register_insert ? kLegacyComponent : kModernComponent);
  void* from = symbols::Find(register_find && register_insert ? kLegacyFrom : kModernFrom);
  void* construct = symbols::Find(register_find && register_insert ? kLegacyConstruct : kModernConstruct);
  void* from_class = register_find && register_insert ? symbols::Find(kLegacyFromClass) : nullptr;
  if (resolves_register_types && resolves_classes && assignable && component && from && construct &&
      (!(register_find && register_insert) || from_class)) {
    const bool legacy = register_find && register_insert;
    hooks->push_back({construct, legacy ? HookAddress(LegacyConstruct) : HookAddress(ModernConstruct),
        legacy ? &original_legacy_construct : &original_modern_construct,
        legacy ? kLegacyConstruct.name : kModernConstruct.name});
    hooks->push_back({assignable, legacy ? HookAddress(LegacyAssignable) : HookAddress(ModernAssignable),
        legacy ? &original_legacy_assignable : &original_modern_assignable,
        legacy ? kLegacyAssignable.name : kModernAssignable.name});
    hooks->push_back({from, legacy ? HookAddress(LegacyFrom) : HookAddress(ModernFrom),
        legacy ? &original_legacy_from : &original_modern_from,
        legacy ? kLegacyFrom.name : kModernFrom.name});
    if (legacy)
      hooks->push_back({from_class, HookAddress(LegacyFromClass), &original_legacy_from_class, kLegacyFromClass.name});
    if (legacy) legacy_component = reinterpret_cast<decltype(legacy_component)>(component);
    else modern_component = reinterpret_cast<decltype(modern_component)>(component);
    resolves_array_assignability = true;
  }
  return true;
}

void BindInterfaceBackend(interface_redefinition::Backend* backend) {
  interfaces.store(backend, std::memory_order_release);
}

bool ResolvesClasses() { return resolves_classes; }
bool ResolvesRegisterTypes() { return resolves_register_types; }
bool ResolvesArrayAssignability() { return resolves_array_assignability; }
}  // namespace artdcevm::verification_redirect

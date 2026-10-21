#include "platform/common/verification_redirect.h"
#include "platform/common/invoke_kind.h"
#include "platform/common/interface_redefinition.h"
#include "evolution/modifiers.h"

#include <android/log.h>
#include <atomic>
#include <string_view>
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

struct State final : transaction::Part {
  const Verification* verifying = nullptr;
  bool register_error = false;
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
bool resolves_register_types = false;
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
  state->register_error = false;
  const bool verified = reinterpret_cast<Function>(original_check_verification)(self, data);
  state->verifying = previous;
  if (state->register_error) {
    self->driver->result = JVMTI_ERROR_INTERNAL;
    return false;
  }
  if (const auto error = (backend ? backend->VerificationError() : JVMTI_ERROR_NONE); error != JVMTI_ERROR_NONE) {
    self->driver->result = error;
    return false;
  }
  return verified;
}

void* VerifiedClass(void* linker, std::string_view descriptor, Handle loader) {
  const State* state = CurrentState();
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
void* RebuiltClass(void* found) {
  const State* state = CurrentState();
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
  explicit PublishedArrayLookup(bool array) : state_(array ? CurrentState() : nullptr),
      previous_(state_ ? std::exchange(state_->verifying, nullptr) : nullptr) {}
  ~PublishedArrayLookup() { if (state_) state_->verifying = previous_; }
 private:
  State* state_;
  const Verification* previous_;
};
const void* RegisterFindClass(const void* cache, void* klass, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::FindClass;
  klass = RegisterClass(klass, &precise);
  return reinterpret_cast<Function>(original_register_find_class)(cache, klass, precise);
}
const void* RegisterInsertClass(void* cache, const std::string_view& descriptor, void* klass, bool precise) noexcept {
  using Function = abi::arm64::android13::verifier::InsertClass;
  klass = RegisterClass(klass, &precise);
  return reinterpret_cast<Function>(original_register_insert_class)(cache, descriptor, klass, precise);
}
const void* RegisterFromClass(void* cache, void* klass) noexcept {
  using Function = abi::arm64::android16::verifier::FromClass;
  return reinterpret_cast<Function>(original_register_from_class)(cache, RegisterClass(klass));
}
void* FindClass(void* linker, void* thread, const char* descriptor, Handle loader) noexcept {
  invoke_kind::ObserveClassLinker(linker);
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, descriptor, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, Handle);
  void* found;
  {
    PublishedArrayLookup scope(descriptor && descriptor[0] == '[');
    found = reinterpret_cast<Function>(original_find_class)(linker, thread, descriptor, loader);
  }
  return RebuiltClass(found);
}
void* FindClassWithLength(void* linker, void* thread, const char* descriptor, size_t length,
                          Handle loader) noexcept {
  invoke_kind::ObserveClassLinker(linker);
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, {descriptor, length}, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, size_t, Handle);
  void* found;
  {
    PublishedArrayLookup scope(descriptor && descriptor[0] == '[');
    found = reinterpret_cast<Function>(original_find_class_with_length)(linker, thread, descriptor, length, loader);
  }
  return RebuiltClass(found);
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
  return true;
}

void BindInterfaceBackend(interface_redefinition::Backend* backend) {
  interfaces.store(backend, std::memory_order_release);
}

bool ResolvesClasses() { return resolves_classes; }
bool ResolvesRegisterTypes() { return resolves_register_types; }
}  // namespace artdcevm::verification_redirect

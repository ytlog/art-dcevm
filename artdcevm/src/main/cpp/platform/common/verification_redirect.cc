#include "platform/common/verification_redirect.h"
#include "platform/common/interface_redefinition.h"

#include <android/log.h>
#include <atomic>
#include <string_view>
#include <utility>

#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/class_layout.h"
#include "platform/common/runtime_thread.h"
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
  // Holder entries of superclass-changed targets, recorded as ART verifies
  // them. ART sorts supertypes first, so before any of their batched subclasses.
  std::vector<int32_t> moved;
  // Holder entries of structural classes on a moved target's new superclass
  // chain: their old classes resolve to the replacements the target links to.
  std::vector<int32_t> chains;
};
State* CurrentState() {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::verification);
}

void* original_check_verification = nullptr;
void* original_find_class = nullptr;
void* original_find_class_with_length = nullptr;
bool resolves_classes = false;
// FindClass hooks read it on any thread; Install publishes it.
std::atomic<interface_redefinition::Backend*> interfaces{nullptr};

bool CheckVerification(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (!target || !target->redirect_verification)
    return reinterpret_cast<Function>(original_check_verification)(self, data);
  // The standard entry creates this state while Native. Without it the new
  // class cannot be shown to ART's verifier: fail rather than verify against
  // the old one. This noexcept hook does not allocate a diagnostic.
  State* state = CurrentState();
  if (!state) {
    __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "verification redirection state is missing");
    self->driver->result = JVMTI_ERROR_INTERNAL;
    return false;
  }
  if (target->superclass) state->moved.push_back(data.index);
  if (!target->chain_rebuilds.empty()) {
    const auto thread = runtime_thread::Ready();
    for (jclass chain : target->chain_rebuilds)
      if (const int32_t entry = thread ? class_layout::HolderEntry(*thread, self, data.holder, chain) : -1; entry >= 0)
        state->chains.push_back(entry);
  }
  interface_redefinition::Backend* backend = interfaces.load(std::memory_order_acquire);
  const bool member = backend && backend->IsAffected(self);
  const Verification scope{&data, target->descriptor,
                           member || (backend && !target->linked_interfaces.empty()) ? backend : nullptr, member};
  const Verification* previous = std::exchange(state->verifying, &scope);
  const bool verified = reinterpret_cast<Function>(original_check_verification)(self, data);
  state->verifying = previous;
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
  // A class outside the interface groups resolves itself to its new class and
  // the rebuilt interfaces it links to their replacements.
  if (!verifying->group_member && descriptor == verifying->descriptor) return verifying->data->NewClassObject();
  if (verifying->interface_group)
    return verifying->interface_group->VerificationClass(linker, descriptor, loader);
  return nullptr;
}
// While a batched subclass verifies, a superclass-changed target resolves,
// through its loader as usual, to the old class: show the verifier its new
// class instead, matched by identity rather than by name.
void* MovedClass(void* found) {
  const State* state = CurrentState();
  const Verification* verifying = state ? state->verifying : nullptr;
  if (!found || !verifying || verifying->interface_group || state->moved.empty()) return found;
  const auto thread = runtime_thread::Ready();
  if (!thread) return found;
  for (int32_t index : state->moved) {
    const RedefinitionDataIter moved{index, verifying->data->holder};
    if (runtime_thread::Read(*thread, moved.Reference(RedefinitionDataIter::kSlotMirrorClass)) == found)
      return runtime_thread::Read(*thread, moved.Reference(RedefinitionDataIter::kSlotNewClassObject));
  }
  if (!state->chains.empty())
    if (void* rebuilt = class_layout::Rebuilt(*thread, verifying->data->holder, found, state->chains)) return rebuilt;
  return found;
}
void* FindClass(void* linker, void* thread, const char* descriptor, Handle loader) noexcept {
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, descriptor, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, Handle);
  return MovedClass(reinterpret_cast<Function>(original_find_class)(linker, thread, descriptor, loader));
}
void* FindClassWithLength(void* linker, void* thread, const char* descriptor, size_t length,
                          Handle loader) noexcept {
  if (auto* backend = interfaces.load(std::memory_order_acquire)) backend->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, {descriptor, length}, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, size_t, Handle);
  return MovedClass(reinterpret_cast<Function>(original_find_class_with_length)(linker, thread, descriptor,
                                                                                length, loader));
}
}  // namespace

void Begin() {
  transaction::Current()->verification = std::make_unique<State>();
}

bool Prepare(std::vector<Hook>* hooks, std::string* failure) {
  using symbols::structural::kCheckVerification;
  using namespace symbols::verification_redirect;
  resolves_classes = false;
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
  return true;
}

void BindInterfaceBackend(interface_redefinition::Backend* backend) {
  interfaces.store(backend, std::memory_order_release);
}

bool ResolvesClasses() { return resolves_classes; }
}  // namespace artdcevm::verification_redirect

#include "platform/common/verification_redirect.h"
#include "platform/common/interface_redefinition.h"

#include <string_view>
#include <utility>

#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/verification_redirect_symbols.h"

namespace artdcevm::verification_redirect {
// The redefinition ART is verifying on this thread (transaction::Context).
struct Verification {
  const abi::arm64::RedefinitionDataIter* data;
  std::string_view descriptor;  // "Lpkg/Name;"
  bool interface_group;
};

namespace {
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::Handle;
using abi::arm64::RedefinitionDataIter;

void* original_check_verification = nullptr;
void* original_find_class = nullptr;
void* original_find_class_with_length = nullptr;
bool resolves_classes = false;
Services art{};

bool CheckVerification(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (!target || !target->redirect_verification)
    return reinterpret_cast<Function>(original_check_verification)(self, data);
  const Verification scope{&data, target->descriptor, (art.interfaces && art.interfaces->IsAffected(self))};
  const Verification* previous = std::exchange(context->verifying, &scope);
  const bool verified = reinterpret_cast<Function>(original_check_verification)(self, data);
  context->verifying = previous;
  if (const auto error = (art.interfaces ? art.interfaces->VerificationError() : JVMTI_ERROR_NONE); error != JVMTI_ERROR_NONE) {
    self->driver->result = error;
    return false;
  }
  return verified;
}

void* VerifiedClass(void* linker, std::string_view descriptor, Handle loader) {
  const transaction::Context* context = transaction::Current();
  const Verification* verifying = context ? context->verifying : nullptr;
  if (!verifying) return nullptr;
  if (verifying->interface_group)
    return art.interfaces->VerificationClass(linker, descriptor, loader);
  return descriptor == verifying->descriptor ? verifying->data->NewClassObject() : nullptr;
}
void* FindClass(void* linker, void* thread, const char* descriptor, Handle loader) noexcept {
  if (art.interfaces) art.interfaces->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, descriptor, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, Handle);
  return reinterpret_cast<Function>(original_find_class)(linker, thread, descriptor, loader);
}
void* FindClassWithLength(void* linker, void* thread, const char* descriptor, size_t length,
                          Handle loader) noexcept {
  if (art.interfaces) art.interfaces->ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, {descriptor, length}, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, size_t, Handle);
  return reinterpret_cast<Function>(original_find_class_with_length)(linker, thread, descriptor,
                                                                     length, loader);
}
}  // namespace

bool Prepare(const Services& services, std::vector<Hook>* hooks, std::string* failure) {
  art = services;
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

void BindInterfaceBackend(interface_redefinition::Backend* backend) { art.interfaces = backend; }

bool ResolvesClasses() { return resolves_classes; }
}  // namespace artdcevm::verification_redirect

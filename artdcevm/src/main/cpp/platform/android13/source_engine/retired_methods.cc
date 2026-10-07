#include "retired_methods.h"
#include "art_method-inl.h"
#include "mirror/executable-inl.h"
#include "reflective_value_visitor.h"
#include "dex/invoke_type.h"
#include "platform/android13/nterp_dispatch.h"

namespace artdcevm::android13::source {
namespace {
void (*retire_method)(void*) = nullptr;
bool (*contains_method)(void*) = nullptr;
void (*require_dispatch_checks)() = nullptr;
}
void RetireMethod(art::ArtMethod* method) {
  retire_method(method);
}
bool IsRetiredMethod(art::ArtMethod* method) {
  return contains_method(method);
}
void RequireDispatchChecks() { require_dispatch_checks(); }
extern "C" void ArtDcevmSourceConfigureRetiredMethods(
    void (*retire)(void*), bool (*contains)(void*)) {
  retire_method = retire;
  contains_method = contains;
}
}

// Configure the transaction notification; dispatch state and Runtime access
// belong to the enhancement library.
namespace dispatch_abi = artdcevm::android13;
static_assert(sizeof(dispatch_abi::DispatchInvokeType) == sizeof(art::InvokeType));
static_assert(static_cast<uint32_t>(dispatch_abi::DispatchInvokeType::kStatic) == art::kStatic);
static_assert(static_cast<uint32_t>(dispatch_abi::DispatchInvokeType::kDirect) == art::kDirect);
static_assert(static_cast<uint32_t>(dispatch_abi::DispatchInvokeType::kVirtual) == art::kVirtual);
static_assert(static_cast<uint32_t>(dispatch_abi::DispatchInvokeType::kSuper) == art::kSuper);
static_assert(static_cast<uint32_t>(dispatch_abi::DispatchInvokeType::kInterface) == art::kInterface);
extern "C" void ArtDcevmSourceConfigureDispatchChecks(void (*require)()) {
  artdcevm::android13::source::require_dispatch_checks = require;
}

// The pinned Executable declaration explicitly grants this source helper access.
// Updating its cached Java modifiers uses the declaration's setter, not offsets.
namespace art {
struct ExecutableOffsets {
  static void RefreshModifiers(ObjPtr<mirror::Executable> object, ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    object->SetAccessFlags(method->GetAccessFlags());
  }
};
}
namespace artdcevm::android13::source {
void UpdateReflectedMethodModifiers(const art::ReflectionSourceInfo& info, art::ArtMethod* method) {
  if (info.GetType() != art::kSourceJavaLangReflectExecutable || IsRetiredMethod(method)) return;
  const auto& heap_info = static_cast<const art::HeapReflectiveSourceInfo&>(info);
  auto object = art::ObjPtr<art::mirror::Executable>::DownCast(heap_info.GetObject());
  art::ExecutableOffsets::RefreshModifiers(object, method);
}
}

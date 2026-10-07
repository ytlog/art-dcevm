#include "retired_methods.h"
#include "retired_fields.h"
#include "platform/android13/retired_member_guard.h"
#include "art_method-inl.h"
#include "handle.h"
#include "mirror/executable-inl.h"
#include "mirror/method_handle_impl-inl.h"
#include "mirror/method_type-inl.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
#include "jvalue.h"
#include "dex/invoke_type.h"
#include "platform/android13/nterp_dispatch.h"
#include "runtime.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"

static_assert(sizeof(art::JValue) == sizeof(uint64_t));

namespace artdcevm::android13::source {
namespace {
void (*retire_method)(void*) = nullptr;
bool (*contains_method)(void*) = nullptr;
size_t (*retired_method_count)() = nullptr;
void (*require_dispatch_checks)() = nullptr;
artdcevm::android13::RetiredMemberGuard guard_method = nullptr;
bool Guard(art::ArtMethod* method, art::Thread* self, art::JValue* result = nullptr)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  return guard_method(method, self, result);
}
}
void RetireMethod(art::ArtMethod* method) {
  retire_method(method);
}
bool IsRetiredMethod(art::ArtMethod* method) {
  return contains_method(method);
}
bool HasRetiredMethods() {
  return retired_method_count() != 0;
}
void RequireDispatchChecks() { require_dispatch_checks(); }
extern "C" void ArtDcevmSourceConfigureRetiredMethods(
    void (*retire)(void*), bool (*contains)(void*), size_t (*count)(),
    artdcevm::android13::RetiredMemberGuard guard) {
  retire_method = retire;
  contains_method = contains;
  retired_method_count = count;
  guard_method = guard;
}
}

extern "C" bool ArtDcevmGuardMethodHandle(void* thread, void* slot, void* result) {
  auto object = static_cast<art::StackReference<art::mirror::Object>*>(slot)->AsMirrorPtr();
  auto handle = art::ObjPtr<art::mirror::MethodHandle>::DownCast(object);
  auto kind = handle->GetHandleKind();
  using Kind = art::mirror::MethodHandle::Kind;
  switch (kind) {
    case Kind::kInvokeDirect:
    case Kind::kInvokeInterface:
    case Kind::kInvokeStatic:
    case Kind::kInvokeVirtual:
      return artdcevm::android13::source::Guard(handle->GetTargetMethod(),
          static_cast<art::Thread*>(thread), static_cast<art::JValue*>(result));
    case Kind::kInvokeSuper: {
      auto* self = static_cast<art::Thread*>(thread);
      art::ArtMethod* target = handle->GetTargetMethod();
      if (artdcevm::android13::source::Guard(target, self,
              static_cast<art::JValue*>(result))) return true;
      auto declaring = target->GetDeclaringClass();
      if (declaring->IsInterface()) {
        auto parameters = handle->GetMethodType()->GetPTypes();
        bool directly_implemented = false;
        if (parameters->GetLength() > 0) {
          auto special_caller = parameters->Get(0);
          directly_implemented = special_caller == declaring;
          for (uint32_t i = 0; !directly_implemented &&
               i < special_caller->NumDirectInterfaces(); ++i) {
            auto direct = special_caller->GetDirectInterface(i);
            directly_implemented = direct != nullptr && declaring->IsAssignableFrom(direct);
          }
        }
        if (!directly_implemented) {
          if (result != nullptr) static_cast<art::JValue*>(result)->SetJ(0);
          self->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                                  "Special caller has lost its direct interface relationship");
          return true;
        }
      } else {
        auto parameters = handle->GetMethodType()->GetPTypes();
        bool related = false;
        if (parameters->GetLength() > 0) {
          auto special_caller = parameters->Get(0);
          related = special_caller == declaring ||
              (special_caller->GetSuperClass() != nullptr &&
               declaring->IsAssignableFrom(special_caller->GetSuperClass()));
        }
        if (!related) {
          if (result != nullptr) static_cast<art::JValue*>(result)->SetJ(0);
          self->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                                  "Special caller has lost its superclass relationship");
          return true;
        }
      }
      return false;
    }
    case Kind::kInstanceGet:
    case Kind::kInstancePut:
    case Kind::kStaticGet:
    case Kind::kStaticPut:
      return artdcevm::android13::source::GuardField(handle->GetTargetField(),
          static_cast<art::Thread*>(thread), static_cast<art::JValue*>(result));
    default: return false;
  }
}

// Keep only a transitional source-declared runtime accessor. The enhancement
// library owns dispatch state, DEX decoding, and the checked resolver call.
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

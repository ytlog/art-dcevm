#include "retired_methods.h"
#include "retired_fields.h"
#include "art_method-inl.h"
#include "handle.h"
#include "mirror/executable-inl.h"
#include "mirror/method_handle_impl-inl.h"
#include "mirror/method_type-inl.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
#include "jvalue.h"
#include "dex/dex_instruction-inl.h"
#include "dex/invoke_type.h"
#include "runtime.h"
#include "linear_alloc.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include <unordered_set>
#include <mutex>

namespace artdcevm::android13::source {
namespace {
std::unordered_set<art::ArtMethod*> retired;
std::mutex retired_mutex;
bool dispatch_checks = false;
bool Guard(art::ArtMethod* method, art::Thread* self, art::JValue* result = nullptr)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (!IsRetiredMethod(method)) return false;
  if (result != nullptr) result->SetJ(0);
  self->ThrowNewException("Ljava/lang/NoSuchMethodError;", "Method removed by ART DCEVM");
  return true;
}
}
void RetireMethod(art::ArtMethod* method) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  retired.insert(method);
}
bool IsRetiredMethod(art::ArtMethod* method) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  return retired.find(method) != retired.end();
}
size_t RetiredMethodCount() {
  std::lock_guard<std::mutex> lock(retired_mutex);
  return retired.size();
}
size_t PruneRetiredMethods(const art::LinearAlloc* allocator) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  size_t removed = 0;
  for (auto it = retired.begin(); it != retired.end(); ) {
    if (!allocator->ContainsUnsafe(*it)) { ++it; continue; }
    it = retired.erase(it);
    ++removed;
  }
  return removed;
}
bool HasRetiredMethods() {
  std::lock_guard<std::mutex> lock(retired_mutex);
  return !retired.empty();
}
void RequireDispatchChecks() { dispatch_checks = true; }
bool NeedsDispatchChecks() { return dispatch_checks; }
}

extern "C" bool ArtDcevmGuardMethod(void* method, void* thread, void* result) {
  return artdcevm::android13::source::Guard(static_cast<art::ArtMethod*>(method),
      thread ? static_cast<art::Thread*>(thread) : art::Thread::Current(),
      static_cast<art::JValue*>(result));
}

extern "C" bool ArtDcevmGuardMethodId(jmethodID id) {
  if (!artdcevm::android13::source::HasRetiredMethods()) return false;
  return artdcevm::android13::source::Guard(art::jni::DecodeArtMethod(id), art::Thread::Current());
}

extern "C" bool ArtDcevmGuardReflection(const void* access, jobject executable) {
  auto& soa = *static_cast<const art::ScopedObjectAccessAlreadyRunnable*>(access);
  auto method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  return artdcevm::android13::source::Guard(method, soa.Self());
}
extern "C" bool ArtDcevmGuardJniExecutable(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  return ArtDcevmGuardReflection(&soa, executable);
}
extern "C" bool ArtDcevmGuardJniMethodId(JNIEnv* env, jmethodID method) {
  art::ScopedObjectAccess soa(env);
  return ArtDcevmGuardMethodId(method);
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

extern "C" [[noreturn]] void ArtDcevmGuardDeliverException(void* thread) {
  static_cast<art::Thread*>(thread)->QuickDeliverException();
}

namespace {
using CheckedResolver = void* (*)(void*, void*, uint32_t, void*, int);
CheckedResolver checked_resolver = nullptr;
}
extern "C" void ArtDcevmSourceConfigureDispatchResolver(void* resolver) {
  checked_resolver = reinterpret_cast<CheckedResolver>(resolver);
}
extern "C" bool ArtDcevmSourceNeedsDispatchChecks() {
  return artdcevm::android13::source::NeedsDispatchChecks();
}
extern "C" bool ArtDcevmGuardNterpInvoke(void* thread, void* caller, uint16_t* dex_pc) {
  if (!artdcevm::android13::source::NeedsDispatchChecks()) return false;
  const auto* inst = art::Instruction::At(dex_pc);
  art::InvokeType type;
  bool range = false;
  switch (inst->Opcode()) {
    case art::Instruction::INVOKE_STATIC_RANGE: range = true; [[fallthrough]];
    case art::Instruction::INVOKE_STATIC: type = art::kStatic; break;
    case art::Instruction::INVOKE_DIRECT_RANGE: range = true; [[fallthrough]];
    case art::Instruction::INVOKE_DIRECT: type = art::kDirect; break;
    case art::Instruction::INVOKE_VIRTUAL_RANGE: range = true; [[fallthrough]];
    case art::Instruction::INVOKE_VIRTUAL: type = art::kVirtual; break;
    case art::Instruction::INVOKE_SUPER_RANGE: range = true; [[fallthrough]];
    case art::Instruction::INVOKE_SUPER: type = art::kSuper; break;
    case art::Instruction::INVOKE_INTERFACE_RANGE: range = true; [[fallthrough]];
    case art::Instruction::INVOKE_INTERFACE: type = art::kInterface; break;
    default: return false;
  }
  uint32_t index = range ? inst->VRegB_3rc() : inst->VRegB_35c();
  // ART's checked resolver handles cached as well as unresolved methods. The
  // nterp helper may inline its unchecked counterpart, so guard this entry too.
  return checked_resolver(art::Runtime::Current()->GetClassLinker(), thread, index,
                          caller, static_cast<int>(type)) == nullptr;
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

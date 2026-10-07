/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Field member replacements below adapt the AOSP mirror/field.cc implementation.
#include "retired_fields.h"
#include "platform/android13/retired_field_registry_abi.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "common_throws.h"
#include "dex/dex_instruction-inl.h"
#include "dex/dex_instruction_utils.h"
#include "handle_scope.h"
#include "mirror/class-inl.h"
#include "mirror/field-inl.h"
#include "mirror/var_handle.h"
#include "method_handles-inl.h"
#include "reflective_value_visitor.h"
#include "scoped_thread_state_change-inl.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include "jvalue.h"
#include "thread-current-inl.h"
#include "runtime.h"
#include "write_barrier.h"

static_assert(sizeof(art::JValue) == sizeof(uint64_t));

namespace artdcevm::android13::source {
namespace {
// The managed Field keeps an opaque token across GC. Its native registry lives
// in the enhancement library and is configured before any source transaction.
artdcevm::android13::RetiredFieldCallbacks field_registry{};
}
void RetireField(art::ArtField* field) {
  field_registry.retire(field);
}

extern "C" void ArtDcevmSourceConfigureRetiredFields(
    const artdcevm::android13::RetiredFieldCallbacks* callbacks) {
  field_registry = *callbacks;
}

extern "C" bool ArtDcevmGuardNterpFinalFieldPut(void* thread, void* caller, uint16_t* dex_pc) {
  using namespace artdcevm::android13::source;
  if (!field_registry.final_guard_enabled()) return false;
  auto* method = static_cast<art::ArtMethod*>(caller);
  auto* self = static_cast<art::Thread*>(thread);
  const auto* inst = art::Instruction::At(dex_pc);
  const bool instance_put = art::IsInstructionIPut(inst->Opcode());
  const bool static_put = art::IsInstructionSPut(inst->Opcode());
  if (!instance_put && !static_put) return false;
  uint32_t index = instance_put ? inst->VRegC_22c() : inst->VRegB_21c();
  art::StackHandleScope<2> hs(self);
  auto cache = hs.NewHandle(method->GetDexCache());
  auto loader = hs.NewHandle(method->GetClassLoader());
  auto* field = art::Runtime::Current()->GetClassLinker()->ResolveFieldJLS(index, cache, loader);
  if (field == nullptr) return self->IsExceptionPending();
  if (field->IsFinal() && !field->CanBeChangedBy(method)) {
    art::ThrowIllegalAccessErrorFinalField(method, field);
    return true;
  }
  return false;
}
bool IsRetiredField(art::ArtField* field) {
  return field_registry.token_for(field) != 0;
}
bool GuardField(art::ArtField* field, art::Thread* thread, art::JValue* result) {
  return field_registry.guard(field, thread, result);
}
}

// These two replacements are compiled as mirror::Field member functions to use
// the pinned source declarations, including their private setters. No layout
// offsets are supplied by this project. The loader hooks the corresponding ELF
// symbols so every Field visitor understands the retirement token.
namespace art::mirror {
ArtField* Field::GetArtField() {
  using namespace artdcevm::android13::source;
  uint32_t index = GetArtFieldIndex();
  if ((index & artdcevm::android13::kRetiredFieldTag) != 0) {
    auto* field = static_cast<ArtField*>(field_registry.resolve(index));
    CHECK(field != nullptr);
    return field;
  }
  auto declaring_class = GetDeclaringClass();
  return IsStatic() ? declaring_class->GetStaticField(index)
                    : declaring_class->GetInstanceField(index);
}
void Field::VisitTarget(ReflectiveValueVisitor* visitor) {
  using namespace artdcevm::android13::source;
  ArtField* original = GetArtField();
  ArtField* replacement = visitor->VisitField(
      original, HeapReflectiveSourceInfo(kSourceJavaLangReflectField, this));
  CHECK(replacement != nullptr);
  uint32_t retired_token = field_registry.token_for(replacement);
  if (retired_token != 0) {
    SetArtFieldIndex<false>(retired_token);
    return;
  }
  if (original == replacement) return;
  SetOffset<false>(replacement->GetOffset().Int32Value());
  SetAccessFlags<false>(replacement->GetAccessFlags());
  SetDeclaringClass<false>(replacement->GetDeclaringClass());
  auto range = IsStatic() ? GetDeclaringClass()->GetSFields() : GetDeclaringClass()->GetIFields();
  auto position = std::find_if(range.begin(), range.end(),
                              [&](const auto& field) { return &field == replacement; });
  CHECK(position != range.end());
  SetArtFieldIndex<false>(std::distance(range.begin(), position));
  WriteBarrier::ForEveryFieldWrite(this);
}
}

extern "C" void* ArtDcevmSourceGetArtField(void* field) {
  return static_cast<art::mirror::Field*>(field)->GetArtField();
}
extern "C" void ArtDcevmSourceVisitField(void* field, void* visitor) {
  static_cast<art::mirror::Field*>(field)->VisitTarget(
      static_cast<art::ReflectiveValueVisitor*>(visitor));
}
extern "C" bool ArtDcevmGuardFieldId(jfieldID field) {
  return artdcevm::android13::source::GuardField(
      art::jni::DecodeArtField(field), art::Thread::Current());
}
extern "C" bool ArtDcevmGuardJniField(JNIEnv* env, jfieldID field) {
  art::ScopedObjectAccess soa(env);
  return ArtDcevmGuardFieldId(field);
}
extern "C" bool ArtDcevmGuardReflectionField(JNIEnv* env, jobject field) {
  art::ScopedObjectAccess soa(env);
  return artdcevm::android13::source::GuardField(
      soa.Decode<art::mirror::Field>(field)->GetArtField(), soa.Self());
}
extern "C" bool ArtDcevmSourceReceiverGuardEnabled();
extern "C" bool ArtDcevmGuardFieldVarHandle(void* handle, int mode, void* frame,
                                             const void* operands, void* result) {
  auto* field_handle = static_cast<art::mirror::FieldVarHandle*>(handle);
  art::ArtField* field = field_handle->GetArtField();
  art::Thread* self = art::Thread::Current();
  if (artdcevm::android13::source::GuardField(field, self, static_cast<art::JValue*>(result)))
    return true;
  using VarHandle = art::mirror::VarHandle;
  if (field->IsFinal() && mode >= 0 &&
      mode <= static_cast<int>(VarHandle::AccessMode::kLast) &&
      VarHandle::GetAccessModeTemplate(static_cast<VarHandle::AccessMode>(mode)) !=
          VarHandle::AccessModeTemplate::kGet) {
    if (result != nullptr) static_cast<art::JValue*>(result)->SetJ(0);
    self->ThrowNewException("Ljava/lang/UnsupportedOperationException;",
                            "Final field does not support VarHandle write access");
    return true;
  }
  if (!ArtDcevmSourceReceiverGuardEnabled() || field->IsStatic()) return false;
  art::ShadowFrameGetter getter(*static_cast<art::ShadowFrame*>(frame),
                                static_cast<const art::InstructionOperands*>(operands));
  art::ObjPtr<art::mirror::Object> receiver = getter.GetReference();
  if (receiver == nullptr || field->GetDeclaringClass()->IsAssignableFrom(receiver->GetClass()))
    return false;
  self->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                          "VarHandle receiver has lost its field declaring class");
  return true;
}
extern "C" bool ArtDcevmGuardMethodHandleFieldPut(void* target_field) {
  art::ArtField* field = static_cast<art::ArtField*>(target_field);
  art::Thread* self = art::Thread::Current();
  if (artdcevm::android13::source::GuardField(field, self)) return true;
  if (!artdcevm::android13::source::field_registry.final_guard_enabled() ||
      !field->IsFinal()) return false;
  self->ThrowNewException("Ljava/lang/IllegalAccessError;",
                          "MethodHandle setter targets a final field");
  return true;
}

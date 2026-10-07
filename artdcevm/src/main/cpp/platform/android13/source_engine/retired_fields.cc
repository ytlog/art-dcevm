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
#include "mirror/class-inl.h"
#include "mirror/field-inl.h"
#include "reflective_value_visitor.h"
#include "write_barrier.h"
#include <algorithm>

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

bool IsRetiredField(art::ArtField* field) {
  return field_registry.token_for(field) != 0;
}
}

// The remaining Field visitor uses the device GetArtField symbol. Its installed
// enhancement hook owns field-array lookup and retirement-token decoding.
namespace art::mirror {
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

extern "C" void ArtDcevmSourceVisitField(void* field, void* visitor) {
  static_cast<art::mirror::Field*>(field)->VisitTarget(
      static_cast<art::ReflectiveValueVisitor*>(visitor));
}

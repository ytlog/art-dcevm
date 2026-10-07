#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "art_field-inl.h"
#include "base/length_prefixed_array.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include <cstring>

static_assert(sizeof(art::ArtField) == sizeof(artdcevm::android13::field_copy_abi::ArtField));
static_assert(alignof(art::ArtField) == alignof(artdcevm::android13::field_copy_abi::ArtField));
static_assert(art::ArtField::OffsetOffset().Uint32Value() ==
    offsetof(artdcevm::android13::field_copy_abi::ArtField, offset));
static_assert(art::ArtField::DeclaringClassOffset().Uint32Value() ==
    offsetof(artdcevm::android13::field_copy_abi::ArtField, declaring_class));
static_assert(art::mirror::Class::SuperClassOffset().Uint32Value() ==
    offsetof(artdcevm::android13::validation_abi::ClassPrefix, super_class));

// Diagnostic only: source getters compare owned results; they never supply
// metadata or create member IDs for the enhancement's copy scheduler.
extern "C" int ArtDcevmSourceProbeFieldCopyMetadata(JNIEnv* env, jobject calibration,
    artdcevm::android13::CopyClassReader read_class, artdcevm::android13::CopyFieldReader read_field) {
  if (art::kIsDebugBuild || art::kObjPtrPoisoning || art::kPoisonHeapReferences ||
      !read_class || !read_field) return 1;
  if (art::LengthPrefixedArray<art::ArtField>::OffsetOfElement(0) !=
      artdcevm::android13::field_copy_abi::kFieldArrayDataOffset ||
      art::LengthPrefixedArray<art::ArtField>::OffsetOfElement(1) !=
      artdcevm::android13::field_copy_abi::kFieldArrayDataOffset + sizeof(art::ArtField)) return 2;
  art::ScopedObjectAccess soa(env);
  auto instance = soa.Decode<art::mirror::Object>(calibration);
  if (instance.IsNull()) return 3;
  art::ObjPtr<art::mirror::Class> klass = instance->GetClass();
  for (; klass != nullptr; klass = klass->GetSuperClass()) {
    std::string storage;
    const char* descriptor = klass->GetDescriptor(&storage);
    for (bool is_static : {false, true}) {
      auto view = read_class(klass.Ptr(), is_static);
      if (view.parent != klass->GetSuperClass().Ptr() || view.loader != klass->GetClassLoader().Ptr() ||
          view.is_object != klass->IsObjectClass() || !view.descriptor ||
          std::strcmp(view.descriptor, descriptor) != 0 || view.field_count !=
              (is_static ? klass->NumStaticFields() : klass->NumInstanceFields())) return 4;
      size_t index = 0;
      for (auto& field : (is_static ? klass->GetSFields() : klass->GetIFields())) {
        auto owned = read_field(klass.Ptr(), is_static, index++);
        if (owned.field != &field || !owned.name || !owned.descriptor ||
            std::strcmp(owned.name, field.GetName()) != 0 ||
            std::strcmp(owned.descriptor, field.GetTypeDescriptor()) != 0) return 5;
        artdcevm::android13::field_copy_abi::ArtField fixed;
        std::memcpy(&fixed, owned.field, sizeof(fixed));
        if (fixed.access_flags != field.GetAccessFlags() || fixed.dex_field_index != field.GetDexFieldIndex() ||
            fixed.offset != field.GetOffset().Uint32Value()) return 6;
      }
    }
  }
  return 0;
}

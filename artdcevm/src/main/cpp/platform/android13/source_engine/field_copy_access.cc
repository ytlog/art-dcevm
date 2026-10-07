#include "platform/android13/field_copy_abi.h"
#include "art_field-inl.h"
#include "dex/dex_file-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "write_barrier-inl.h"
#include <cstring>

namespace {
using namespace artdcevm::android13;
EvolutionFieldCopy copy = nullptr;
CopyClassView Class(void* raw, bool is_static) {
  art::ObjPtr<art::mirror::Class> klass(reinterpret_cast<art::mirror::Class*>(raw));
  return {klass->GetSuperClass().Ptr(), klass->GetClassLoader().Ptr(),
          klass->IsObjectClass() ? "Ljava/lang/Object;" :
              klass->GetDexFile().StringByTypeIdx(klass->GetDexTypeIndex()),
          is_static ? klass->NumStaticFields() : klass->NumInstanceFields(),
          klass->IsObjectClass()};
}
CopyFieldView Field(void* raw, bool is_static, size_t index) {
  art::ObjPtr<art::mirror::Class> klass(reinterpret_cast<art::mirror::Class*>(raw));
  auto fields = is_static ? klass->GetSFields() : klass->GetIFields();
  auto* field = &*(fields.begin() + index);
  return {field, field->GetName(), field->GetTypeDescriptor()};
}
template<class T> uint64_t Pack(T value) {
  static_assert(sizeof(T) <= sizeof(uint64_t));
  uint64_t bits = 0; std::memcpy(&bits, &value, sizeof(value)); return bits;
}
template<class T> T Unpack(uint64_t bits) {
  T value; std::memcpy(&value, &bits, sizeof(value)); return value;
}
uint64_t Read(void* object, void* raw_field, char type) {
  auto* field = static_cast<art::ArtField*>(raw_field);
  art::ObjPtr<art::mirror::Object> source(reinterpret_cast<art::mirror::Object*>(object));
  switch (type) {
    case 'L': case '[': return reinterpret_cast<uintptr_t>(field->GetObject(source).Ptr());
#define READ(TYPE, CODE) case CODE: return Pack(field->Get##TYPE(source))
    READ(Boolean, 'Z'); READ(Byte, 'B'); READ(Char, 'C'); READ(Short, 'S');
    READ(Int, 'I'); READ(Float, 'F'); READ(Long, 'J'); READ(Double, 'D');
#undef READ
    default: LOG(FATAL) << "Invalid copied field type " << type; UNREACHABLE();
  }
}
void Write(void* object, void* raw_field, char type, uint64_t bits) {
  auto* field = static_cast<art::ArtField*>(raw_field);
  art::ObjPtr<art::mirror::Object> target(reinterpret_cast<art::mirror::Object*>(object));
  switch (type) {
    case 'L': case '[':
      field->SetObject<false>(target, art::ObjPtr<art::mirror::Object>(
          reinterpret_cast<art::mirror::Object*>(static_cast<uintptr_t>(bits))));
      return;
#define WRITE(TYPE, CPP, CODE) case CODE: field->Set##TYPE<false>(target, Unpack<CPP>(bits)); return
    WRITE(Boolean, uint8_t, 'Z'); WRITE(Byte, int8_t, 'B'); WRITE(Char, uint16_t, 'C');
    WRITE(Short, int16_t, 'S'); WRITE(Int, int32_t, 'I'); WRITE(Float, float, 'F');
    WRITE(Long, int64_t, 'J'); WRITE(Double, double, 'D');
#undef WRITE
    default: LOG(FATAL) << "Invalid copied field type " << type; UNREACHABLE();
  }
}
void Monitor(void* new_object, void* old_object) {
  art::ObjPtr<art::mirror::Object> target(reinterpret_cast<art::mirror::Object*>(new_object));
  art::ObjPtr<art::mirror::Object> source(reinterpret_cast<art::mirror::Object*>(old_object));
  target->SetLockWord(source->GetLockWord(false), false);
  source->SetLockWord(art::LockWord::Default(), false);
}
void Barrier(void* object) {
  art::WriteBarrier::ForEveryFieldWrite(art::ObjPtr<art::mirror::Object>(
      reinterpret_cast<art::mirror::Object*>(object)));
}
}
extern "C" void ArtDcevmSourceConfigureFieldCopy(EvolutionFieldCopy callback, FieldCopyAccess* access) {
  *access = {Class, Field, Read, Write, Monitor, Barrier};
  copy = callback;
}
extern "C" void ArtDcevmSourceCopyAndClearFields(bool is_static, void* target, void* target_class,
                                                void* source, void* source_class) {
  copy(is_static, target, target_class, source, source_class);
}

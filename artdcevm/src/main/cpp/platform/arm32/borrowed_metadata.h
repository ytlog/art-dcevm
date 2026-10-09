#pragma once
#include "platform/common/canonical_jni_members.h"
#include "platform/common/arm32_method_metadata.h"
#include "platform/common/readonly_field_table.h"
#include <vector>
namespace artdcevm::arm32_borrowed_metadata {
// Typed source services valid only during the actual original JNI callback.
// Future evolution consumers must keep their roots and finish all reads before
// returning; these views never imply mutating/GC/allocator capability.
struct Methods {
 void* array=nullptr;size_t stride=0;uint32_t declared=0,total=0;
 canonical_jni_members::Method At(uint32_t index)const {
  return {index<total?static_cast<unsigned char*>(array)+4+size_t(index)*stride:nullptr};
 }
 bool Identity(uint32_t index,method_identity_reader::Identity* output)const {
  const auto source=stride==28?arm32_method_metadata::kAndroid11:arm32_method_metadata::kAndroid12Onward;
  return (stride==24||stride==28)&&index<total&&arm32_method_metadata::ReadIdentity(source,At(index).address,stride,output);
 }
};
struct Fields {
 std::vector<canonical_jni_members::Field> declarations;
 bool Storage(uint32_t index,uint32_t* flags,uint32_t* object_offset)const {
  if(index>=declarations.size()||!flags||!object_offset)return false;
  const auto* field=static_cast<const unsigned char*>(declarations[index].address);
  if(!field||uintptr_t(field)%4||uintptr_t(field)>UINTPTR_MAX-16)return false;
  *flags=__atomic_load_n(reinterpret_cast<const uint32_t*>(field+4),__ATOMIC_RELAXED);
  std::memcpy(object_offset,field+offsetof(readonly_field_table::Field,offset),4);return true;
 }
};
struct Services {Methods methods;Fields fields;};
using Consumer=bool (*)(const Services&,void*);
}

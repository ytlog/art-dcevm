#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>
namespace artdcevm::abi::arm64::android15 {
// Fixed 15--17 ClassRedefinition declaration. Opaque platform string/ArrayRef.
struct ClassRedefinition {
  void* driver; jclass klass; const void* dex_file;
  alignas(void*) unsigned char class_signature[24];
  alignas(void*) unsigned char original_dex_file[16];
  bool added_fields, added_methods, has_virtuals;

};
static_assert(sizeof(ClassRedefinition)==72 && offsetof(ClassRedefinition,added_fields)==64 && offsetof(ClassRedefinition,has_virtuals)==66);
static_assert(offsetof(ClassRedefinition,class_signature)==24 && offsetof(ClassRedefinition,original_dex_file)==48);
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>

namespace artdcevm::abi::arm64::android_common {
// android-16.0.0_r1 openjdkjvmti/ti_class_definition.h, blob
// 3c1660eb6df8f6982365a476d12348c5be2d333a. Borrowed native storage only;
// no platform string/vector is constructed, mutated or destroyed here.
struct TransformedDefinition {
 jclass klass;
 jobject loader;
 alignas(8) unsigned char name[24];
 jobject protection_domain;
 const unsigned char* owned_begin;
 const unsigned char* owned_end;
 const unsigned char* owned_capacity;
 const unsigned char* dex_begin;
 size_t dex_size;
 const unsigned char* current_owned_begin;
 const unsigned char* current_owned_end;
 const unsigned char* current_owned_capacity;
 const unsigned char* current_begin;
 size_t current_size;
 uint8_t redefined,initialized,structural_update;
};
struct TransformedDefinitions {
 const TransformedDefinition* begin;
 const TransformedDefinition* end;
 const TransformedDefinition* capacity;
};
static_assert(sizeof(TransformedDefinition)==136);
static_assert(offsetof(TransformedDefinition,dex_begin)==72);
static_assert(offsetof(TransformedDefinition,initialized)==129);
static_assert(sizeof(TransformedDefinitions)==24);
constexpr char kTransformStructural[]=
 "_ZN12openjdkjvmti11Transformer22CallClassFileLoadHooksILNS_16RedefinitionTypeE0EEEvPN3art6ThreadEPNSt3__16vectorINS_18ArtClassDefinitionENS6_9allocatorIS8_EEEE";
}

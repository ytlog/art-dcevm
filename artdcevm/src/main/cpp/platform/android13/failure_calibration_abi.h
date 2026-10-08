#pragma once
#include "platform/android13/redefinition_art_abi.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace artdcevm::android13::failure_calibration_abi {
// android-13.0.0_r1 openjdkjvmti/ti_redefine.h: Redefiner's constructor
// initializes INTERNAL, Structural, an empty vector and a borrowed error pointer.
// RecordFailure accesses only result/error; no platform container is constructed
// or destroyed through NDK declarations.
struct PlatformString { alignas(8) unsigned char bytes[24]{}; };
struct Driver {
 void* environment=nullptr;
 int32_t result=113; // JVMTI_ERROR_INTERNAL
 uint32_t result_padding=0;
 void* runtime=nullptr;
 void* self=nullptr;
 int32_t type=0; // RedefinitionType::kStructural
 uint32_t type_padding=0;
 void* redefinitions_begin=nullptr;
 void* redefinitions_end=nullptr;
 void* redefinitions_capacity=nullptr;
 PlatformString* error=nullptr;
};
inline constexpr char kStringDestroy[]="_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev";
inline constexpr char kDeclarationId[]="android13-source-family-arm64-release";
static_assert(std::is_standard_layout_v<Driver> && std::is_trivially_destructible_v<Driver>);
static_assert(sizeof(PlatformString)==24 && alignof(PlatformString)==8);
static_assert(sizeof(Driver)==72 && alignof(Driver)==8);
static_assert(offsetof(Driver,result)==8 && offsetof(Driver,type)==32 && offsetof(Driver,error)==64);
static_assert(offsetof(Driver,redefinitions_begin)==40);
static_assert(offsetof(Driver,environment)==0 && offsetof(Driver,runtime)==16 && offsetof(Driver,self)==24);
static_assert(offsetof(Driver,redefinitions_end)==48 && offsetof(Driver,redefinitions_capacity)==56);
using Definition=redefinition_abi::ClassRedefinition;
static_assert(sizeof(Definition)==72 && alignof(Definition)==8);
static_assert(offsetof(Definition,driver)==0 && offsetof(Definition,klass)==8 && offsetof(Definition,dex_file)==16);
static_assert(offsetof(Definition,class_signature)==24 && offsetof(Definition,original_dex_file)==48);
static_assert(offsetof(Definition,added_fields)==64 && offsetof(Definition,added_methods)==65);
static_assert(offsetof(Definition,has_virtuals)==66 && offsetof(Definition,lock_acquired)==67);
static_assert(sizeof(redefinition_abi::DexFilePrefix)==24 && offsetof(redefinition_abi::DexFilePrefix,begin)==8 &&
              offsetof(redefinition_abi::DexFilePrefix,size)==16);
}

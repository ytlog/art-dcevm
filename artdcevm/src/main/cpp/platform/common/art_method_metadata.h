#pragma once
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android12/art_method.h"
#include <cstddef>
#include <cstdint>
#include "platform/common/method_identity_reader.h"
namespace artdcevm::method_metadata {
// Fixed release arm64 runtime/art_method.h declarations. Matching/calibrating
// the device declaration and keeping methods alive remain provider duties.
using Method11 = abi::arm64::android11::Method11;
using Method12 = abi::arm64::android12::Method12;
static_assert(sizeof(Method11)==40 && offsetof(Method11,dex_method_index)==12);
static_assert(offsetof(Method11,data)==24 && offsetof(Method11,entry_point)==32);
static_assert(sizeof(Method12)==32 && offsetof(Method12,dex_method_index)==8);
static_assert(offsetof(Method12,data)==16 && offsetof(Method12,entry_point)==24);
static_assert(offsetof(Method11,access_flags)==4 && offsetof(Method12,access_flags)==4);
struct Layout {size_t size,dex_method_index,data,entry_point;};
inline constexpr Layout kAndroid11{sizeof(Method11),offsetof(Method11,dex_method_index),offsetof(Method11,data),offsetof(Method11,entry_point)};
inline constexpr Layout kAndroid12Onward{sizeof(Method12),offsetof(Method12,dex_method_index),offsetof(Method12,data),offsetof(Method12,entry_point)};
// ArtMethod::GetAccessFlags uses a relaxed atomic load even under the shared
// mutator lock: verifier/JIT flags may be updated concurrently after linking.
using method_identity_reader::ReadAccessFlags;
using Identity=method_identity_reader::Identity;
// Reads only identity fields. Mutable hotness/entry/data fields are deliberately
// not included in a non-atomic whole-object snapshot; declaring class needs ART's
// read barrier. Caller provides a verified live mapping extent.
inline bool ReadIdentity(const Layout& declaration,const void* method,size_t extent,Identity* output) {
  return method_identity_reader::ReadIdentity(declaration.size,
      declaration.dex_method_index,method,extent,output);
}
}

#pragma once
#include "platform/abi/arm32/android11/art_method.h"
#include "platform/abi/arm32/android12/art_method.h"
#include "platform/common/method_identity_reader.h"
namespace artdcevm::arm32_method_metadata {
using Method11=abi::arm32::android11::Method11;
using Method12=abi::arm32::android12::Method12;
using Identity=method_identity_reader::Identity;
struct Layout {size_t size,dex_method_index,data,entry_point;};
inline constexpr Layout kAndroid11{28,12,20,24};
inline constexpr Layout kAndroid12Onward{24,8,16,20};
static_assert(offsetof(Method11,dex_method_index)==12);
static_assert(offsetof(Method12,dex_method_index)==8);
using method_identity_reader::ReadAccessFlags;
inline bool ReadIdentity(const Layout& declaration,const void* method,size_t extent,
                         Identity* output) {
  return method_identity_reader::ReadIdentity(declaration.size,
      declaration.dex_method_index,method,extent,output);
}
}

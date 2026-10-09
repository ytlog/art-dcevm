#include "platform/common/art_method_metadata.h"
#include "platform/common/arm32_method_metadata.h"
#include "platform/abi/arm32/android-common/jni_access.h"
#include "platform/abi/arm32/android-common/suspend_all.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
namespace m32=artdcevm::arm32_method_metadata;
namespace m64=artdcevm::method_metadata;
namespace common=artdcevm::abi::common;
// Golden source field order, deliberately independent from layout descriptors.
// This also exercises array strides: reading a second 32-bit method through a
// 64-bit declaration either exceeds its extent or reads different bytes.
static void Fixture(bool legacy,bool narrow) {
  alignas(8) std::array<unsigned char,80> bytes{};
  const size_t stride=legacy?(narrow?28:40):(narrow?24:32);
  auto* second=bytes.data()+stride;
  uint32_t flags=0x01000101,index=0x76543210,code_offset=0x22222222;
  std::memcpy(second+4,&flags,4);
  if(legacy)std::memcpy(second+8,&code_offset,4);
  std::memcpy(second+(legacy?12:8),&index,4);
  // JNI entry/data are deliberately unrelated to the method's DEX identity.
  std::memset(second+(legacy?(narrow?20:24):16),0xfd,narrow?8:16);
  artdcevm::method_identity_reader::Identity result{7,9};
  if(narrow) {
    const auto layout=legacy?m32::kAndroid11:m32::kAndroid12Onward;
    assert(m32::ReadIdentity(layout,second,stride,&result));
    assert(!m64::ReadIdentity(legacy?m64::kAndroid11:m64::kAndroid12Onward,
                              second,stride,&result));
  } else {
    assert(m64::ReadIdentity(legacy?m64::kAndroid11:m64::kAndroid12Onward,
                            second,stride,&result));
  }
  assert(result.access_flags==flags && result.dex_method_index==index);
}
int main() {
  for(bool legacy:{false,true})for(bool narrow:{false,true})Fixture(legacy,narrow);
  alignas(4) std::array<unsigned char,28> method{};
  m32::Identity sentinel{11,22};
  for(auto layout:{m32::kAndroid11,m32::kAndroid12Onward}) {
    assert(!m32::ReadIdentity(layout,method.data(),layout.size-1,&sentinel));
    assert(!m32::ReadIdentity(layout,method.data()+1,28,&sentinel));
    assert(!m32::ReadIdentity(layout,nullptr,28,&sentinel));
    assert(!m32::ReadIdentity(layout,reinterpret_cast<const void*>(UINTPTR_MAX-3),28,&sentinel));
    assert(!m32::ReadIdentity(layout,method.data(),28,nullptr));
    layout.dex_method_index=layout.size;
    assert(!m32::ReadIdentity(layout,method.data(),28,&sentinel));
    layout.size=0;
    assert(!m32::ReadIdentity(layout,method.data(),28,&sentinel));
  }
  assert(sentinel.access_flags==11 && sentinel.dex_method_index==22);
  static_assert(std::is_same<common::FromReflectedMethod,
      void* (*)(const common::RunnableJniScope&,jobject)>::value);
  static_assert(std::is_same<common::SuspendConstruct,
      void (*)(void*,const char*,bool)>::value);
  // Fixed-width scope carriers must preserve all three independent values.
  common::RunnableJniScope32 small{0x12345678,0x98765432,0xfedcba98};
  common::RunnableJniScope64 large{0x1234567812345678,0x9876543298765432,0xfedcba98fedcba98};
  uint32_t env32;uint64_t env64;
  std::memcpy(&env32,reinterpret_cast<const unsigned char*>(&small)+4,4);
  std::memcpy(&env64,reinterpret_cast<const unsigned char*>(&large)+8,8);
  assert(env32==0x98765432 && env64==0x9876543298765432);
#if defined(__arm__)
  static_assert(sizeof(void*)==4 && sizeof(common::RunnableJniScope)==12);
#endif
  std::cout<<"CPU method metadata: independent 32/64 source fixtures, array strides, bounds and JNI declarations passed\n";
}

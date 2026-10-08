#include "platform/common/reflection_metadata.h"
#include "platform/common/field_array_view.h"
#include <array>
#include <cassert>
#include <iostream>
namespace g=artdcevm::reflection_metadata;
int main() {
  alignas(8) std::array<unsigned char,40> bytes{};
  auto put=[&](size_t offset,uint32_t value){std::memcpy(bytes.data()+offset,&value,4);};
  put(12,0x11111111);put(16,0x22222222);put(20,0x89abcdef);put(24,0x80000007);put(28,0xfedcba98);
  for(int api=30;api<=37;++api) {
    g::FieldIdentity value{};
    assert(g::ReadField(api,bytes.data(),32,&value));
    assert(value.access_flags==0x89abcdef && value.array_index==0x80000007 && value.offset==0xfedcba98);
    g::FieldIdentity sentinel{1,2,3};
    for(size_t n=0;n<32;++n)assert(!g::ReadField(api,bytes.data(),n,&sentinel));
    assert(!g::ReadField(api,bytes.data()+1,32,&sentinel));
    assert(sentinel.access_flags==1 && sentinel.array_index==2 && sentinel.offset==3);
    const uint64_t pointer=0x123456789abcdef0;
    std::memcpy(bytes.data()+24,&pointer,8);
    void* method=nullptr;
    assert(g::ReadExecutableMethod(api,bytes.data(),40,&method) && reinterpret_cast<uintptr_t>(method)==pointer);
    for(size_t n=0;n<40;++n)assert(!g::ReadExecutableMethod(api,bytes.data(),n,&method));
    assert(reinterpret_cast<uintptr_t>(method)==pointer);
    put(24,0x80000007);put(28,0xfedcba98);
  }
  // Confirm the genuinely different accessible/parameter fields, not just sizes.
  using E11=artdcevm::abi::arm64::android11::Executable;
  using E12=artdcevm::abi::arm64::android12::Executable;
  static_assert(sizeof(E11::has_real_parameter_data)==2 && sizeof(E12::has_real_parameter_data)==1);
  static_assert(offsetof(E11,has_real_parameter_data)==10 && offsetof(E12,has_real_parameter_data)==9);
  static_assert(offsetof(artdcevm::abi::arm64::android11::ReflectedField,padding)==10);
  static_assert(offsetof(artdcevm::abi::arm64::android12::ReflectedField,padding)==9);
  // A modern mirror stores physical index 3, not static filtered ordinal 1.
  alignas(4) std::array<unsigned char,68> fields{};
  uint32_t count=4;std::memcpy(fields.data(),&count,4);
  for(uint32_t i=0;i<4;++i){uint32_t flags=i%2?8:1;std::memcpy(fields.data()+4+16*i+4,&flags,4);}
  put(20,8);put(24,3);g::FieldIdentity reflected;
  assert(g::ReadField(37,bytes.data(),32,&reflected));
  artdcevm::field_array::View view;
  assert(artdcevm::field_array::Read(37,fields.data(),fields.size(),true,&view));
  assert(view.At(reflected.array_index)==view.AtSelected(1));
  uint32_t physical=99;assert(view.IndexOf(view.At(reflected.array_index),&physical) && physical==3);
  g::FieldIdentity sentinel{1,2,3};void* method=reinterpret_cast<void*>(0xabc);
  for(int api:{29,38}) {
    assert(!g::ReadField(api,bytes.data(),32,&sentinel));
    assert(!g::ReadExecutableMethod(api,bytes.data(),40,&method));
  }
  assert(!g::ReadField(37,nullptr,32,&sentinel));
  assert(!g::ReadField(37,reinterpret_cast<void*>(UINTPTR_MAX-3),32,&sentinel));
  assert(!g::ReadField(37,bytes.data(),32,nullptr));
  assert(!g::ReadExecutableMethod(37,bytes.data()+1,40,&method));
  assert(!g::ReadExecutableMethod(37,nullptr,40,&method));
  assert(!g::ReadExecutableMethod(37,reinterpret_cast<void*>(UINTPTR_MAX-3),40,&method));
  assert(!g::ReadExecutableMethod(37,bytes.data(),40,nullptr));
  assert(sentinel.access_flags==1 && sentinel.array_index==2 && sentinel.offset==3 && method==reinterpret_cast<void*>(0xabc));
  uint64_t null_method=0;std::memcpy(bytes.data()+24,&null_method,8);
  assert(g::ReadExecutableMethod(37,bytes.data(),40,&method) && method==nullptr);
  std::cout<<"Reflection metadata: API30-37 fixed families, scalar/root separation, retired bits, physical merged index, valid null methods and bounded failures passed\n";
}

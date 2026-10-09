#include "platform/common/art_method_metadata.h"
#include "platform/abi/common/jni_access.h"
#include "platform/abi/common/suspend_all.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
namespace metadata=artdcevm::method_metadata;
namespace common=artdcevm::abi::common;
// Independent fixed ARM64 source field order, including the second array entry.
static void Fixture(bool legacy) {
 alignas(8) std::array<unsigned char,80> bytes{};
 const size_t stride=legacy?40:32;auto* second=bytes.data()+stride;
 const uint32_t flags=0x01000101,index=0x76543210;
 std::memcpy(second+4,&flags,4);std::memcpy(second+(legacy?12:8),&index,4);
 std::memset(second+(legacy?24:16),0xfd,16);
 metadata::Identity result{7,9};auto declaration=legacy?metadata::kAndroid11:metadata::kAndroid12Onward;
 assert(metadata::ReadIdentity(declaration,second,stride,&result));
 assert(result.access_flags==flags&&result.dex_method_index==index);
 result={11,22};
 assert(!metadata::ReadIdentity(declaration,second,stride-1,&result));
 assert(!metadata::ReadIdentity(declaration,second+1,stride,&result));
 assert(!metadata::ReadIdentity(declaration,nullptr,stride,&result));
 assert(!metadata::ReadIdentity(declaration,reinterpret_cast<const void*>(UINTPTR_MAX-3),stride,&result));
 assert(!metadata::ReadIdentity(declaration,second,stride,nullptr));
 declaration.dex_method_index=declaration.size;
 assert(!metadata::ReadIdentity(declaration,second,stride,&result));
 declaration.size=0;assert(!metadata::ReadIdentity(declaration,second,stride,&result));
 assert(result.access_flags==11&&result.dex_method_index==22);
}
int main(){
 Fixture(false);Fixture(true);
 static_assert(sizeof(void*)==8&&sizeof(common::RunnableJniScope)==24);
 static_assert(std::is_same<common::FromReflectedMethod,void* (*)(const common::RunnableJniScope&,jobject)>::value);
 static_assert(std::is_same<common::SuspendConstruct,void (*)(void*,const char*,bool)>::value);
 std::cout<<"ARM64 method metadata: independent source fixtures, array strides, bounds and JNI declarations passed\n";
}

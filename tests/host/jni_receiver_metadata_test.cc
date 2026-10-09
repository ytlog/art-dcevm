#include "platform/common/jni_receiver_metadata.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::jni_receiver_metadata;
int main() {
  for(size_t width:{4,8}) {
    alignas(8) std::array<unsigned char,24> bytes{};
    // Independent serialized JNIEnvExt: table, self, vm; not a constructed JNI scope.
    const uint64_t self=width==4?0x87654321ULL:0x0123456787654321ULL;
    const uint64_t vm=width==4?0xfedcba98ULL:0x12345678fedcba98ULL;
    std::memcpy(bytes.data()+width,&self,width);
    std::memcpy(bytes.data()+2*width,&vm,width);
    Receiver result{11,22};
    assert(ReadMatchedPrefix(width,bytes.data(),3*width,self,vm,&result));
    assert(result.self==self && result.vm==vm);
    // Mutating/check-JNI table is deliberately not part of the owner proof.
    std::memset(bytes.data(),0xfd,width);
    assert(ReadMatchedPrefix(width,bytes.data(),3*width,self,vm,&result));
    const Receiver sentinel{33,44};result=sentinel;
    assert(!ReadMatchedPrefix(width,bytes.data(),3*width,self+1,vm,&result));
    assert(!ReadMatchedPrefix(width,bytes.data(),3*width,self,vm+1,&result));
    assert(!ReadMatchedPrefix(width,bytes.data(),3*width-1,self,vm,&result));
    assert(!ReadMatchedPrefix(width,bytes.data()+1,24,self,vm,&result));
    assert(!ReadMatchedPrefix(width,nullptr,24,self,vm,&result));
    assert(!ReadMatchedPrefix(width,bytes.data(),24,0,vm,&result));
    assert(!ReadMatchedPrefix(width,bytes.data(),24,self,0,&result));
    assert(!ReadMatchedPrefix(width,reinterpret_cast<const void*>(UINTPTR_MAX-width+1),24,self,vm,&result));
    assert(!ReadMatchedPrefix(width,bytes.data(),24,self,vm,nullptr));
    assert(!ReadMatchedPrefix(2,bytes.data(),24,self,vm,&result));
    if(width==4)assert(!ReadMatchedPrefix(width,bytes.data(),24,0x100000000ULL,vm,&result));
    assert(result.self==sentinel.self && result.vm==sentinel.vm);
  }
  std::cout<<"JNI receiver metadata: fixed 32/64 prefixes, independent owners and failure preservation passed\n";
}

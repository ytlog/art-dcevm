#include "platform/common/source_cache_method_witness.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
alignas(8) unsigned char storage[40]{};
jobject expected=reinterpret_cast<jobject>(0x1100);
void* self=reinterpret_cast<void*>(0x2200);
JNIEnv* env=reinterpret_cast<JNIEnv*>(0x3300);
void* vm=reinterpret_cast<void*>(0x4400);
size_t calls=0;bool readable=true,returns=true;size_t available=40;
void* Leaf(const abi::common::RunnableJniScope& scope,jobject method){
 assert(scope.self==self&&scope.env==env&&scope.vm==vm&&method==expected);++calls;
 return returns?storage:nullptr;
}
bool Extent(const void* value,size_t* size){assert(value==storage);*size=available;return readable;}
void Set(const method_metadata::Layout& layout,uint32_t flags,uint32_t index){
 std::memset(storage,0,sizeof(storage));std::memcpy(storage+4,&flags,4);
 std::memcpy(storage+layout.dex_method_index,&index,4);
}
}
int main(){
 for(const auto layout:{method_metadata::kAndroid11,method_metadata::kAndroid12Onward}){
  Set(layout,1,7);runnable_jni::Context context{self,env,vm};void* result=nullptr;std::string error;
  // Real GlobalRef contexts carry no original_method. Reflected fallback carries
  // Object.toString instead. Both must independently read fixture.read.
  assert(source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error)&&result==storage);
  context.original_method=reinterpret_cast<void*>(0xdead);
  assert(source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error));
  result=reinterpret_cast<void*>(0xbeef);
  assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,8,Extent,&result,&error)&&result==reinterpret_cast<void*>(0xbeef)&&!error.empty());
  Set(layout,9,7);assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error));
  assert(source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error,9u));
  Set(layout,1,7);assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error,9u));
  Set(layout,1,7);available=8;assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error));available=40;
  readable=false;assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error));readable=true;
  returns=false;assert(!source_cache_method_witness::Read(context,Leaf,expected,layout,7,Extent,&result,&error));returns=true;
  size_t old=calls;assert(!source_cache_method_witness::Read(context,nullptr,expected,layout,7,Extent,&result,&error)&&calls==old);
 }
 std::cout<<"Cache fixture method witness: independent actual leaf for both Runnable routes; mismatched DEX/access/extent/missing leaf rejected\n";
}

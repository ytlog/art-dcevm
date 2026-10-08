#include "platform/common/runtime_services.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sanitizer/asan_interface.h>
using namespace artdcevm;
using runtime_services::Declaration;
using runtime_services::View;
template<class Prefix> void Test(Declaration declaration) {
  Prefix runtime{};
  int items[7]{};
  runtime.class_linker=&items[0];runtime.jni_id_manager=&items[1];
  runtime.jit=&items[2];runtime.jit_code_cache=&items[3];runtime.thread_list=&items[4];
  runtime.heap=&items[5];runtime.java_vm=&items[6];
  // Source-private roots/containers/startup atomic fields are unread. Poisoning
  // them catches copying a whole concurrently mutable Runtime prefix.
  __asan_poison_memory_region(&runtime,sizeof(runtime));
  auto allow=[](void* field){__asan_unpoison_memory_region(field,sizeof(void*));};
  allow(&runtime.class_linker);allow(&runtime.jni_id_manager);allow(&runtime.jit);
  allow(&runtime.jit_code_cache);allow(&runtime.thread_list);allow(&runtime.heap);allow(&runtime.java_vm);
  View view;
  assert(runtime_services::Read(declaration,&runtime,sizeof(runtime),&view));
  assert(view.class_linker==&items[0] && view.jni_id_manager==&items[1] && view.jit==&items[2]);
  assert(view.jit_code_cache==&items[3] && view.thread_list==&items[4] && view.heap==&items[5] && view.java_vm==&items[6]);
  runtime.jit=nullptr;
  assert(runtime_services::Read(declaration,&runtime,sizeof(runtime),&view));
  assert(!view.jit && !view.jit_code_cache);
  runtime.jit=&items[2];runtime.jni_id_manager=nullptr;
  assert(runtime_services::Read(declaration,&runtime,sizeof(runtime),&view) && !view.jni_id_manager);
  view.class_linker=&items[0];
  assert(!runtime_services::Read(declaration,&runtime,sizeof(runtime)-1,&view) && view.class_linker==&items[0]);
  assert(!runtime_services::Read(declaration,nullptr,sizeof(runtime),&view) && view.class_linker==&items[0]);
  assert(!runtime_services::Read(declaration,&runtime,sizeof(runtime),nullptr));
  assert(!runtime_services::Read(declaration,reinterpret_cast<unsigned char*>(&runtime)+1,sizeof(runtime),&view));
  assert(!runtime_services::Read(declaration,reinterpret_cast<void*>(UINTPTR_MAX-7),sizeof(runtime),&view));
  assert(!runtime_services::Read(static_cast<Declaration>(99),&runtime,sizeof(runtime),&view));
  assert(view.class_linker==&items[0]);
  __asan_unpoison_memory_region(&runtime,sizeof(runtime));
}
int main(){
 Test<abi::arm64::android11::RuntimePrefix>(Declaration::kAndroid11);
 Test<abi::arm64::android12::RuntimePrefix>(Declaration::kAndroid12);
 Test<abi::arm64::android12::RuntimePrefix>(Declaration::kAndroid12); // 12L
 Test<abi::arm64::android13::RuntimePrefix>(Declaration::kAndroid13);
 Test<abi::arm64::android14::RuntimePrefix>(Declaration::kAndroid14);
 for(int api=35;api<=37;++api)Test<abi::arm64::android15::RuntimePrefix>(Declaration::kAndroid15To17);
 std::cout<<"Runtime service view: eight sources, isolated pointer reads, null JIT and failed output preservation passed\n";
}

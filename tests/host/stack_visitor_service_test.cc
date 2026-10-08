#include "stack_visitor_native_fixture.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace artdcevm;
using stack_visitor_service::Declaration;
using stack_visitor_service::Service;
using stack_visitor_service::WalkKind;
template<class Storage> struct Callback final:Storage {
  unsigned callbacks=0;
  size_t stop_after=1;
  bool VisitFrame() override {++callbacks;return callbacks<stop_after;}
};
template<class Storage> void Test(Declaration declaration) {
  static_assert(sizeof(Storage)%8==0);
  Callback<Storage> visitor;
  Service service;
  auto finder=[&](const char* name){return stack_fixture::Symbol(declaration,name);};
  assert(Service::StorageSize(declaration)==sizeof(Storage));
  assert(Service::Bind(declaration,finder,&service));
  int thread_storage=0,context_storage=0,method_storage=0;
  void* thread=&thread_storage;
  void* context=&context_storage;
  void* vptr;
  std::memcpy(&vptr,static_cast<const void*>(&visitor),sizeof(vptr));
  auto& observed=stack_fixture::observed;
  unsigned calls=observed.constructs;
  assert(!service.Construct(&visitor,sizeof(Storage)-1,thread,context,WalkKind::kIncludeInlined));
  assert(!service.Construct(&visitor,sizeof(Storage),nullptr,context,WalkKind::kIncludeInlined));
  assert(!service.Construct(&visitor,sizeof(Storage),thread,context,static_cast<WalkKind>(99)));
  assert(!service.Construct(reinterpret_cast<void*>(UINTPTR_MAX-7),sizeof(Storage),thread,context,WalkKind::kIncludeInlined));
  assert(!service.Construct(reinterpret_cast<unsigned char*>(&visitor)+1,sizeof(Storage),thread,context,WalkKind::kIncludeInlined));
  assert(observed.constructs==calls);
  for(bool checked:{false,true}) {
    for(WalkKind kind:{WalkKind::kIncludeInlined,WalkKind::kSkipInlined}) {
      assert(service.Construct(&visitor,sizeof(Storage),thread,context,kind,checked));
      assert(observed.thread==thread && observed.context==context && observed.kind==kind && observed.check==checked);
      assert(visitor.thread==thread && visitor.context==context && visitor.kind==kind && visitor.check_suspended==checked);
      void* after;
      std::memcpy(&after,static_cast<const void*>(&visitor),sizeof(after));
      assert(after==vptr);
    }
  }
  // Context may be null for a walk which does not need register restoration.
  assert(service.Construct(&visitor,sizeof(Storage),thread,nullptr,WalkKind::kSkipInlined));
  assert(!visitor.context);
  void* method=reinterpret_cast<void*>(uintptr_t{7});
  assert(service.Method(&visitor,sizeof(Storage),&method) && method==nullptr);
  void* expected=&method_storage;
  visitor.quick_frame=&expected;
  assert(service.Method(&visitor,sizeof(Storage),&method) && method==expected);
  calls=observed.methods;
  assert(!service.Method(&visitor,sizeof(Storage)-1,&method) && method==expected);
  assert(!service.Method(nullptr,sizeof(Storage),&method) && method==expected);
  assert(!service.Method(&visitor,sizeof(Storage),nullptr));
  assert(observed.methods==calls);
  for(bool transitions:{false,true}) {
    visitor.callbacks=0;visitor.stop_after=2;
    assert(service.Walk(&visitor,sizeof(Storage),transitions));
    assert(observed.transitions==transitions && visitor.callbacks==2 && visitor.depth==1);
  }
  calls=observed.walks;
  assert(!service.Walk(&visitor,sizeof(Storage)-1));
  assert(!service.Walk(nullptr,sizeof(Storage)));
  assert(observed.walks==calls);
  // Every missing required name preserves an already working binding.
  for(const char* missing:{Service::ConstructSymbol(),Service::WalkSymbol(declaration),Service::MethodSymbol()}) {
    assert(!Service::Bind(declaration,[&](const char* name){
      return std::strcmp(name,missing)==0 ? nullptr : finder(name);
    },&service));
    visitor.callbacks=0;visitor.stop_after=1;
    assert(service.Walk(&visitor,sizeof(Storage)) && visitor.callbacks==1);
  }
  unsigned lookups=0;
  assert(!Service::Bind(static_cast<Declaration>(99),[&](const char*)->void*{++lookups;return nullptr;},&service));
  assert(!Service::Bind(declaration,[&](const char*)->void*{++lookups;return nullptr;},nullptr));
  assert(!lookups);
  assert(!Service::StorageSize(static_cast<Declaration>(99)));
  assert(!Service::WalkSymbol(static_cast<Declaration>(99)));
  Service unbound;
  assert(!unbound.Walk(&visitor,sizeof(Storage)));
  assert(!unbound.Method(&visitor,sizeof(Storage),&method) && method==expected);
}
}
int main() {
  Test<abi::arm64::stack::Visitor11>(Declaration::kAndroid11);
  Test<abi::arm64::stack::Visitor12>(Declaration::kAndroid12);
  // Source 12.1 shares the Android 12 declaration.
  Test<abi::arm64::stack::Visitor12>(Declaration::kAndroid12);
  for(int api=33;api<=36;++api) Test<abi::arm64::stack::Visitor13>(Declaration::kAndroid13To16);
  Test<abi::arm64::stack::Visitor17>(Declaration::kAndroid17);
  assert(std::strcmp(Service::WalkSymbol(Declaration::kAndroid17),
      "_ZN3art12StackVisitor9WalkStackILNS0_16CountTransitionsE0ELb0EEEvb")==0);
  std::cout<<"stack visitor service: eight source families, full storage, native virtual callbacks, vptr restoration, names and failure preservation passed\n";
}

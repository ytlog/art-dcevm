#include "platform/common/thread_suspend_service.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace {
using namespace artdcevm::thread_suspend_service;
using namespace artdcevm::abi::arm64;
std::vector<std::string> events;
bool suspended=false;
void* storage_seen=nullptr;
const char* cause_seen=nullptr;
bool long_seen=false;
void Native(void* self) {
  assert((__atomic_load_n(static_cast<uint32_t*>(self),__ATOMIC_RELAXED)>>24)==0);
  __atomic_store_n(static_cast<uint32_t*>(self),uint32_t{92}<<24,__ATOMIC_RELAXED);events.push_back("native");
}
void Runnable(void* self) {
  assert(!suspended && (__atomic_load_n(static_cast<uint32_t*>(self),__ATOMIC_RELAXED)>>24)==92);
  __atomic_store_n(static_cast<uint32_t*>(self),uint32_t{0},__ATOMIC_RELAXED);events.push_back("runnable");
}
void Construct(void* storage,const char* cause,bool long_suspend) {
  assert(!suspended);suspended=true;storage_seen=storage;cause_seen=cause;long_seen=long_suspend;events.push_back("suspend");
}
void Destroy(void* storage) {
  assert(suspended && storage==storage_seen);suspended=false;events.push_back("resume");
}
void* Find(const char* name,bool transitions=true,const char* missing=nullptr) {
  if(missing && std::strcmp(name,missing)==0) return nullptr;
  if(std::strcmp(name,android_common::kSuspendConstruct)==0) return reinterpret_cast<void*>(Construct);
  if(std::strcmp(name,android_common::kSuspendDestroy)==0) return reinterpret_cast<void*>(Destroy);
  if(transitions && std::strcmp(name,android13::kToNative)==0) return reinterpret_cast<void*>(Native);
  if(transitions && std::strcmp(name,android13::kToRunnable)==0) return reinterpret_cast<void*>(Runnable);
  return nullptr;
}
}
int main() {
  for(int api=30;api<=37;++api) {
    const auto declaration=api<=32 ? Declaration::kLegacy16 : Declaration::kModern24;
    const unsigned shift=api<=32 ? 16 : 24;
    const uint32_t runnable=api<=32 ? 67 : 0;
    Service service;
    assert(Service::Bind(declaration,[](const char* name){return Find(name);},&service));
    assert(service.CanTransitionRunnable()==(api>=33));
    alignas(4) uint32_t word=(92u<<shift)|((uint32_t{1}<<shift)-1);
    uint32_t state=999;
    assert(service.State(&word,sizeof(word),&state) && state==92);
    assert(!service.State(nullptr,4,&state) && state==92);
    assert(!service.State(&word,3,&state) && state==92);
    assert(!service.State(reinterpret_cast<void*>(UINTPTR_MAX-3),4,&state) && state==92);
    assert(!service.State(reinterpret_cast<unsigned char*>(&word)+1,4,&state) && state==92);
    assert(!service.State(&word,4,nullptr));
    bool unchanged=true;
    assert(!service.EnterNative(nullptr,4,&unchanged) && unchanged);
    assert(!service.EnterNative(&word,4,nullptr));
    assert(!service.RestoreRunnable(nullptr));
    assert(!service.RestoreRunnable(reinterpret_cast<unsigned char*>(&word)+1));
    assert(!service.RestoreRunnable(reinterpret_cast<void*>(UINTPTR_MAX-3)));
    android_common::SuspendAll raw_storage{};
    assert(!service.ConstructSuspend(nullptr,1,"unused",false));
    assert(!service.ConstructSuspend(&raw_storage,0,"unused",false));
    assert(!service.ConstructSuspend(reinterpret_cast<void*>(UINTPTR_MAX),1,"unused",false));
    assert(!service.DestroySuspend(nullptr,1));
    assert(!service.DestroySuspend(&raw_storage,0));
    assert(!service.DestroySuspend(reinterpret_cast<void*>(UINTPTR_MAX),1));
    events.clear();
    const char* cause="source scope ordering";
    {NativeScope already_native(service,&word,4);assert(already_native.Supported());
      {SuspendScope suspend(service,cause,true);assert(suspend.Supported());assert(cause_seen==cause && long_seen);}}
    assert((events==std::vector<std::string>{"suspend","resume"}));
    word=runnable<<shift;events.clear();
    {NativeScope scope(service,&word,4);assert(scope.Supported()==(api>=33));
      if(scope.Supported()) {
        {NativeScope nested(service,&word,4);assert(nested.Supported());}
        {SuspendScope suspend(service,cause,false);assert(suspend.Supported());assert(!long_seen);}
      }}
    assert(word==runnable<<shift);
    assert(api<=32 ? events.empty() : (events==std::vector<std::string>{"native","suspend","resume","runnable"}));
    word=93u<<shift;events.clear();
    {NativeScope unsupported(service,&word,4);assert(!unsupported.Supported());}
    assert(events.empty());
    for(const char* missing:{android13::kToNative,android13::kToRunnable}) {
      Service partial;
      assert(Service::Bind(declaration,[&](const char* name){return Find(name,true,missing);},&partial));
      assert(!partial.CanTransitionRunnable());
      word=runnable<<shift;
      {NativeScope no_transition(partial,&word,4);assert(!no_transition.Supported());}
      word=92u<<shift;
      {NativeScope native(partial,&word,4);assert(native.Supported());}
    }
    for(const char* missing:{android_common::kSuspendConstruct,android_common::kSuspendDestroy}) {
      assert(!Service::Bind(declaration,[&](const char* name){return Find(name,true,missing);},&service));
      assert(service.CanTransitionRunnable()==(api>=33));
    }
    assert(!Service::Bind(static_cast<Declaration>(99),[](const char* name){return Find(name);},&service));
    assert(!Service::Bind(declaration,[](const char* name){return Find(name);},nullptr));
    Service empty;
    state=999;
    assert(!empty.State(&word,4,&state) && state==999);
    {NativeScope no_binding(empty,&word,4);assert(!no_binding.Supported());}
    {SuspendScope no_binding(empty,cause);assert(!no_binding.Supported());}
    {SuspendScope no_cause(service,nullptr);assert(!no_cause.Supported());}
    // Concurrent flags publication uses the complete state-and-flags word.
    // The owning thread remains Native; this does not claim arbitrary state CAS.
    word=92u<<shift;
    std::atomic<bool> start{false};
    std::thread writer([&]{while(!start.load(std::memory_order_acquire)) {}
      for(uint32_t i=0;i<10000;++i) __atomic_store_n(&word,(92u<<shift)|(i&((1u<<shift)-1)),__ATOMIC_RELAXED);});
    start.store(true,std::memory_order_release);
    for(unsigned i=0;i<10000;++i) assert(service.State(&word,4,&state) && state==92);
    writer.join();
  }
  std::cout<<"thread suspend service: eight source models, state flags, native nesting, reversible named transitions, suspend ordering and missing-capability boundaries passed\n";
}

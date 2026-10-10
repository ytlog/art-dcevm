#include "platform/common/heap_collect_observer.h"
#include "platform/abi/common/suspend_all.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
namespace artdcevm::heap_collect_observer {
namespace {
using Collect=void (*)(void*,bool,Cause);
Collect original=nullptr;
std::array<std::atomic<Observer>,8> observers{};
std::mutex installation;
bool installed=false,attempted=false;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
void Proxy(void* heap,bool clear,Cause cause) {
  // No lock is taken from an ART mutator/GC call. Registration only publishes
  // immortal static callbacks; callbacks own synchronization and request scopes.
  for(auto& slot:observers)if(auto observe=slot.load(std::memory_order_acquire))observe(heap,clear,cause);
  original(heap,clear,cause);
}
}
bool Install(HookEngine& hooks,Observer observer,std::string* error) {
  if(error)error->clear();
  if(!observer)return Fail(error,"Missing Heap collector observation subscriber");
  std::lock_guard<std::mutex> guard(installation);
  if(!installed) {
    if(attempted)return Fail(error,"Heap collector observation installation failed; restart process");
    using namespace abi::common;
    auto target=hooks.FindSymbol("libart.so",kCollect);
    auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
    auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
    if(!target||!construct||!destroy)return Fail(error,"Missing named Heap collector observation declarations");
    attempted=true;
    SuspendAll storage{};construct(&storage,"artdcevm shared Heap collector observation",false);
    void* trampoline=nullptr;
    auto token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&trampoline,error);
    if(token&&!trampoline)std::abort();
    original=reinterpret_cast<Collect>(trampoline);
    installed=token&&original;
    destroy(&storage);
    if(!installed)return Fail(error,"Heap collector observation hook unavailable; restart process");
  }
  for(auto& slot:observers)if(slot.load(std::memory_order_acquire)==observer)return true;
  for(auto& slot:observers)if(!slot.load(std::memory_order_relaxed)) {
    slot.store(observer,std::memory_order_release);return true;
  }
  return Fail(error,"Heap collector observation subscriber capacity exhausted");
}
}

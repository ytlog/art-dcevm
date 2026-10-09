#pragma once
#include <cstddef>
namespace artdcevm::owned_jit_compilation {
// Shared fixed Jit declarations in Android 11--17 jit.h. The 11--13 source
// implementations already expose this same queue/drain ABI; their internal
// task kinds/baseline booleans do not enter this caller's argument list.
// Availability is selected by exact names, not SDK. Queue requires shared
// mutator access; Wait requires the
// same current Thread outside that access (ThreadPool::Wait may block).
inline constexpr char kEnqueue[]="_ZN3art3jit3Jit27EnqueueOptimizedCompilationEPNS_9ArtMethodEPNS_6ThreadE";
inline constexpr char kWait[]="_ZN3art3jit3Jit26WaitForCompilationToFinishEPNS_6ThreadE";
class Service {
 public:
  template<class Finder> static bool Bind(Finder find,Service* output) {
    if(!output)return false;Service value;
    value.enqueue_=reinterpret_cast<Enqueue>(find(kEnqueue));
    value.wait_=reinterpret_cast<Wait>(find(kWait));
    if(!value.enqueue_||!value.wait_)return false;*output=value;return true;
  }
  // Receiver/method/thread ownership and locks are independent prerequisites.
  // Request may be a source-defined no-op (no pool, single-tier/already compiled
  // policy). Queue/drain/cache membership never prove optimized inline shape;
  // the independent live native header/maps/two-inline/root proof is mandatory.
  bool Queue(void* jit,void* rooted_method,void* current_thread) const {
    if(!enqueue_||!jit||!rooted_method||!current_thread)return false;
    enqueue_(jit,rooted_method,current_thread);return true;
  }
  bool Drain(void* jit,void* current_thread) const {
    if(!wait_||!jit||!current_thread)return false;
    wait_(jit,current_thread);return true;
  }
 private:
  using Enqueue=void (*)(void*,void*,void*);
  using Wait=void (*)(void*,void*);
  Enqueue enqueue_=nullptr;Wait wait_=nullptr;
};
}

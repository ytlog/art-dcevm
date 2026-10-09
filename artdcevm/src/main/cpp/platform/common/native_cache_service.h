#pragma once
#include "api/hook_engine.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::native_cache {
// Borrow the real native function-static closure, via its named symbol or
// observation of its owner's named RunCheckpoint call. It is never constructed,
// deleted or stored in a fabricated ART Thread. Requires independently observed
// ThreadList identity, followed by Prepare from a regular Native JNI caller.
class Service {
 public:
  bool Bind(HookEngine&,void* observed_thread_list,std::string* error);
  bool Prepare(std::string* error);
  bool Prepared()const{return prepared_;}
  bool PreparedForCurrentThread()const;
  // Caller already owns ART's final exclusive mutator boundary. Acquires the
  // real ThreadList mutex and synchronously executes native clearing on each
  // currently registered Thread; no asynchronous checkpoint or Thread offset.
  // Readonly source-stop scanner preparation. The caller must independently
  // own the continuous exclusive mutator stop; Prepared() does not prove that
  // ownership. Callback must not use JNI, suspend, throw, or mutate ART state.
  // Registered count includes all threads even when a callback refuses; further
  // callbacks are skipped after refusal. Native ThreadList mutex remains held.
  using StoppedVisitor=bool (*)(void* thread,void* state);
  bool VisitStoppedWorld(StoppedVisitor,void* state,size_t* registered_threads,std::string* error)const;
  bool ClearStoppedWorld(size_t* cleared_threads,std::string* error) const;
 private:
  void* closure_=nullptr;
  void* threads_=nullptr;
  void** list_lock_=nullptr;
  void (*prepare_)()=nullptr;
  void* (*current_)()=nullptr;
  void (*lock_)(void*,void*)=nullptr;
  void (*vendor_lock_)(void*,void*,uint32_t)=nullptr;
  void* vendor_lock_self_=nullptr;
  uint32_t vendor_lock_argument_=0;
  bool vendor_lock_observed_=false;
  void (*unlock_)(void*,void*)=nullptr;
  void (*for_each_)(void*,void (*)(void*,void*),void*)=nullptr;
  bool prepared_=false;
};
}

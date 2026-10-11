#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/runtime_services.h"
#include "platform/common/runnable_jni_bridge.h"
#include <cstdint>
#include <string>

namespace artdcevm::native_source_compilation {
class Service;
// Receipt of an original ScopedJitSuspend constructor that actually returned
// on this thread. It owns no JIT, native scope, method or managed reference.
class Receipt final {
 public:
 Receipt()=default;
 private:
 friend class Service;
 const Service*owner_=nullptr;void*scope_=nullptr;void*self_=nullptr;
 uint64_t ticket_=0,epoch_=0;
};
// Observe ART's existing RedefineClasses compilation boundary. Never create a
// second pause, invoke Jit::Stop/Start, or manipulate a code cache. Original
// constructor completion settles already-running compilation; any observed
// destructor or direct Jit::Start invalidates receipts before it can restart
// compilation workers. These hooks forward originals without changing policy.
class Service final {
 public:
 Service()=default;~Service();
 Service(const Service&)=delete;Service&operator=(const Service&)=delete;
 bool Bind(HookEngine&,const runtime_services::Snapshot&,std::string*);
 bool Install(HookEngine&,const native_source_stop::Session&,void*self,std::string*);
 bool Capture(const runnable_jni::Context&,Receipt*,const char**failure=nullptr)const noexcept;
 bool Validate(const Receipt&,void*self)const noexcept;
 private:
 friend struct Hooks;
 using Leaf=void(*)(void*);using Current=void*(*)();
 void*runtime_=nullptr;void**runtime_slot_=nullptr;Current current_=nullptr;
 Leaf construct_=nullptr,destroy_=nullptr,original_construct_=nullptr,original_destroy_=nullptr;
 Leaf start_=nullptr,original_start_=nullptr;
 void*construct_token_=nullptr;void*destroy_token_=nullptr;void*start_token_=nullptr;
};
}

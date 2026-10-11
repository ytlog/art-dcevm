#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_receiver_source_observer.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/runtime_services.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/common/native_pending_exception.h"
#include "platform/common/native_final_write_guard.h"
#include "platform/common/retired_field_registry.h"
#include "platform/common/method_identity_calibration.h"
#include <memory>
namespace artdcevm::receiver_guard {
// Sole process-owned DexPcMoved/field dispatcher. Installation proves no invoke or
// Guard coverage. Actual original events feed the immutable source observer
// and then the existing sole Guard consumer, without a second listener stack.
class ListenerOwner final {
 public:
 explicit ListenerOwner(const SourceObservation&);
 ~ListenerOwner();
 ListenerOwner(const ListenerOwner&)=delete;ListenerOwner&operator=(const ListenerOwner&)=delete;
 bool Bind(HookEngine&,const runtime_services::Snapshot&,stack_visitor_service::Declaration,
           const method_identity::Calibration&,retirement::RetiredFieldRegistry&,
           const native_pending_exception::Service&,final_write_guard::Guard&,std::string*);
 bool Install(const native_source_stop::Session&,void*self,std::string*);
 bool Installed()const noexcept{return installed_;}
 private:
 struct Listener;
 using Add=void(*)(void*,void*,uint32_t,bool);
 const SourceObservation&observer_;
 const native_pending_exception::Service*pending_=nullptr;
 retirement::RetiredFieldRegistry*fields_=nullptr;
 final_write_guard::Guard*writer_=nullptr;
 std::unique_ptr<Listener>listener_;
 Add add_=nullptr;void*runtime_=nullptr;void**runtime_slot_=nullptr;void*instrumentation_=nullptr;
 abi::arm64::android11::CurrentThread current_=nullptr;
 bool bound_=false,claimed_=false,installed_=false;
};
}

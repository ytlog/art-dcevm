#include "platform/common/native_receiver_listener.h"
#include "platform/common/native_receiver_guard.h"
#include "platform/common/modern_field_guards.h"
#include <atomic>
#include <cstdlib>
#include <type_traits>
#include <cstring>
namespace artdcevm::receiver_guard {
namespace {
// Fixed android-16.0.0_r1/runtime/instrumentation.h. MethodUnwind has no
// Handle argument here. Do not reuse the earlier listener's virtual signature.
struct OptionalFrame {const void*frame;bool engaged;};
struct Handle {uint32_t*slot;};
struct ShadowFrame;
struct ListenerDeclaration {
 virtual ~ListenerDeclaration()=default;
 virtual void MethodEntered(void*,void*)=0;
 virtual void MethodExited(void*,void*,OptionalFrame,Handle&)=0;
 virtual void MethodExited(void*,void*,OptionalFrame,uint64_t&)=0;
 virtual void MethodUnwind(void*,void*,uint32_t)=0;
 virtual void DexPcMoved(void*,Handle,void*,uint32_t)=0;
 virtual void FieldRead(void*,Handle,void*,uint32_t,void*)=0;
 virtual void FieldWritten(void*,Handle,void*,uint32_t,void*,Handle)=0;
 virtual void FieldWritten(void*,Handle,void*,uint32_t,void*,const uint64_t&)=0;
 virtual void ExceptionThrown(void*,Handle)=0;
 virtual void ExceptionHandled(void*,Handle)=0;
 virtual void Branch(void*,void*,uint32_t,int32_t)=0;
 virtual void WatchedFramePop(void*,const ShadowFrame&)=0;
};
static_assert(sizeof(OptionalFrame)==16&&alignof(OptionalFrame)==8&&std::is_trivially_copyable_v<OptionalFrame>);
static_assert(sizeof(Handle)==8&&std::is_trivially_copyable_v<Handle>);
constexpr char kAdd[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb";
constexpr uint32_t kDexPcMoved=0x8u;
constexpr uint32_t kFieldRead=0x10u,kFieldWritten=0x20u;
std::atomic<ListenerOwner*>process{nullptr};
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
}
struct ListenerOwner::Listener final:ListenerDeclaration {
 explicit Listener(ListenerOwner&owner):owner(owner){}
 ListenerOwner&owner;
 void MethodEntered(void*,void*)override{}
 void MethodExited(void*,void*,OptionalFrame,Handle&)override{}
 void MethodExited(void*,void*,OptionalFrame,uint64_t&)override{}
 void MethodUnwind(void*,void*,uint32_t)override{}
 void DexPcMoved(void*self,Handle,void*method,uint32_t pc)override{
  if(!self||!method||!owner.current_||owner.current_()!=self)return;
  bool pending=false;if(!owner.pending_||!owner.pending_->Read(self,&pending))std::abort();
  if(pending)return;
  // ART supplies this event under shared mutator ownership. The observer
  // independently checks the staged exact thread/method/public instruction.
  owner.observer_.DexPcMoved(self,method,pc);
  if(!owner.pending_->Read(self,&pending))std::abort();if(pending)return;
  auto*guard=consumer.load(std::memory_order_acquire);
  if(guard)guard->CheckBytecode(self,method,pc);
 }
 void Field(void*self,Handle receiver,void*method,void*field,bool write,bool reference=false){
  if(!self||!method||!field||!owner.current_||owner.current_()!=self||!owner.pending_||
     !owner.fields_||!owner.writer_||!retirement::FieldEntryGuardsOwn(*owner.fields_))std::abort();
  bool pending=false;if(!owner.pending_->Read(self,&pending))std::abort();
  if(pending)return; // Preserve a previous listener/original exception.
  if(retirement::RejectRetiredField(field,self,nullptr))return;
  const auto source=owner.writer_->SourceOfEvent(self,method);
  if(source!=final_write_guard::EventSource::Bytecode)return;
  auto*guard=consumer.load(std::memory_order_acquire);
  if(guard){
   uint32_t receiver_reference=0;
   if(receiver.slot)std::memcpy(&receiver_reference,receiver.slot,sizeof(receiver_reference));
   // The event's Handle tracks GC; decode freshly in the sole receiver Guard.
   if(guard->CheckCompressed(write?Path::BytecodeWrite:Path::BytecodeRead,
       self,field,receiver_reference,nullptr,true))return;
  }
  if(!owner.pending_->Read(self,&pending))std::abort();if(pending)return;
  // Receiver checking precedes permissions. Both write overloads reach this
  // same owner, but only their exact overload can satisfy a staged writer proof.
  if(write)owner.writer_->FieldWritten(self,method,field,reference);
 }
 void FieldRead(void*self,Handle receiver,void*method,uint32_t,void*field)override{Field(self,receiver,method,field,false);}
 void FieldWritten(void*self,Handle receiver,void*method,uint32_t,void*field,Handle)override{Field(self,receiver,method,field,true,true);}
 void FieldWritten(void*self,Handle receiver,void*method,uint32_t,void*field,const uint64_t&)override{Field(self,receiver,method,field,true);}
 void ExceptionThrown(void*,Handle)override{}
 void ExceptionHandled(void*,Handle)override{}
 void Branch(void*,void*,uint32_t,int32_t)override{}
 void WatchedFramePop(void*,const ShadowFrame&)override{}
};
ListenerOwner::ListenerOwner(const SourceObservation&observer):observer_(observer){}
ListenerOwner::~ListenerOwner(){if(claimed_||installed_)std::abort();}
bool ListenerOwner::Bind(HookEngine&hooks,const runtime_services::Snapshot&runtime,
                         stack_visitor_service::Declaration declaration,const method_identity::Calibration&methods,
                         retirement::RetiredFieldRegistry&fields,const native_pending_exception::Service&pending,final_write_guard::Guard&writer,std::string*error){
 if(bound_||declaration!=stack_visitor_service::Declaration::kAndroid13To16||
    !runtime.runtime||!runtime.instrumentation||!runtime.receivers.java_vm||!pending.Calibrated()||
    !retirement::FieldEntryGuardsOwn(fields)||methods.identities.size()<3||methods.readable_extents.size()!=methods.identities.size()||
    (methods.model!=method_identity::Model::kMethod11&&methods.model!=method_identity::Model::kMethod12Onward))
  return Fail(error,"receiver-listener-fixed-source-or-independent-runtime-unavailable");
 auto add=reinterpret_cast<Add>(hooks.FindSymbol("libart.so",kAdd));
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 auto slot=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 if(!add||!current||!slot||*slot!=runtime.runtime)
  return Fail(error,"receiver-listener-named-fixed-source-or-runtime-slot-unavailable");
 auto listener=std::make_unique<Listener>(*this);
 add_=add;current_=current;runtime_=runtime.runtime;runtime_slot_=slot;instrumentation_=runtime.instrumentation;
 fields_=&fields;pending_=&pending;writer_=&writer;
 listener_=std::move(listener);bound_=true;return true;
}
bool ListenerOwner::Install(const native_source_stop::Session&stop,void*self,std::string*error){
 if(!bound_||!listener_||!self||current_()!=self||!runtime_slot_||*runtime_slot_!=runtime_||!stop.IsStopped(self))
  return Fail(error,"receiver-listener-installation-needs-actual-stop-and-runtime-owner");
 if(installed_)return process.load(std::memory_order_acquire)==this||Fail(error,"receiver-listener-process-owner-changed");
 if(claimed_)return Fail(error,"receiver-listener-prior-partial-installation-requires-new-process");
 ListenerOwner*expected=nullptr;
 if(!process.compare_exchange_strong(expected,this,std::memory_order_acq_rel))
  return Fail(error,"receiver-listener-already-has-process-owner");
 // Claim before the native call: a partial native list allocation must never
 // leave ART holding a destroyed callback. This owner is now process-lifetime.
 claimed_=true;add_(instrumentation_,listener_.get(),kDexPcMoved|kFieldRead|kFieldWritten,false);
 if(current_()!=self||!stop.IsStopped(self)||*runtime_slot_!=runtime_)std::abort();
 installed_=true;return true;
}
}

#include "platform/common/native_source_compilation.h"
#include <array>
#include <atomic>
#include <cstdlib>

namespace artdcevm::native_source_compilation {
namespace {
constexpr char kConstruct[]="_ZN3art3jit16ScopedJitSuspendC1Ev";
constexpr char kConstructAlias[]="_ZN3art3jit16ScopedJitSuspendC2Ev";
constexpr char kDestroy[]="_ZN3art3jit16ScopedJitSuspendD1Ev";
constexpr char kDestroyAlias[]="_ZN3art3jit16ScopedJitSuspendD2Ev";
constexpr char kCurrent[]="_ZN3art6Thread14CurrentFromGdbEv";
constexpr char kStart[]="_ZN3art3jit3Jit5StartEv";
std::atomic<Service*>owner{nullptr};
std::atomic<uint64_t>epoch{1};
std::atomic<size_t>restarts{0};
struct Frame {void*scope=nullptr;void*self=nullptr;uint64_t ticket=0,epoch=0;};
// Bounded observer storage only. An untracked deep scope never authorizes a
// transaction and its destructor still invalidates every outstanding receipt.
thread_local std::array<Frame,32>frames{};
thread_local size_t depth=0;
thread_local uint64_t next_ticket=0;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
void Invalidate()noexcept{if(epoch.fetch_add(1,std::memory_order_acq_rel)==UINT64_MAX)std::abort();}
class Restart final {
 public:
 Restart(){if(restarts.fetch_add(1,std::memory_order_acq_rel)==SIZE_MAX)std::abort();Invalidate();}
 ~Restart(){Invalidate();restarts.fetch_sub(1,std::memory_order_acq_rel);}
 Restart(const Restart&)=delete;Restart&operator=(const Restart&)=delete;
};
}
struct Hooks {
 static void Construct(void*scope){
  auto*service=owner.load(std::memory_order_acquire);
  if(!service||!service->original_construct_)std::abort();
  // No permission exists during the original's potentially suspending wait.
  service->original_construct_(scope);
  if(!scope||!service->runtime_slot_||*service->runtime_slot_!=service->runtime_||depth==frames.size()){
   Invalidate();return;
  }
  void*self=service->current_();if(!self){Invalidate();return;}
  if(++next_ticket==0)std::abort();
  frames[depth++]={scope,self,next_ticket,epoch.load(std::memory_order_acquire)};
 }
 static void Destroy(void*scope){
  auto*service=owner.load(std::memory_order_acquire);
  if(!service||!service->original_destroy_)std::abort();
  // Invalidate first: original Start may let another thread compile before
  // this destructor returns. Foreign/pre-installation scopes also restart JIT.
  Restart restart;
  for(size_t i=depth;i>0;--i)if(frames[i-1].scope==scope){
   for(size_t j=i;j<depth;++j)frames[j-1]=frames[j];
   frames[--depth]={};break;
  }
  service->original_destroy_(scope);
 }
 static void Start(void*jit){
  auto*service=owner.load(std::memory_order_acquire);
  if(!service||!service->original_start_)std::abort();
  Restart restart;service->original_start_(jit);
 }
};
Service::~Service(){if(construct_token_||destroy_token_||start_token_)std::abort();}
bool Service::Bind(HookEngine&hooks,const runtime_services::Snapshot&runtime,std::string*error){
 if(runtime_)return Fail(error,"source-compilation-binding-repeated");
 auto slot=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 auto construct=hooks.FindSymbol("libart.so",kConstruct),destroy=hooks.FindSymbol("libart.so",kDestroy);
 auto current=reinterpret_cast<Current>(hooks.FindSymbol("libart.so",kCurrent));
 auto start=hooks.FindSymbol("libart.so",kStart);
 if(!runtime.runtime||!runtime.instrumentation||!slot||*slot!=runtime.runtime||!current||!construct||!destroy||!start||
    construct==destroy||construct==start||destroy==start||
    hooks.FindSymbol("libart.so",kConstructAlias)!=construct||hooks.FindSymbol("libart.so",kDestroyAlias)!=destroy)
  return Fail(error,"source-compilation-named-original-boundary-or-alias-unavailable");
 runtime_=runtime.runtime;runtime_slot_=slot;current_=current;
 construct_=reinterpret_cast<Leaf>(construct);destroy_=reinterpret_cast<Leaf>(destroy);start_=reinterpret_cast<Leaf>(start);return true;
}
bool Service::Install(HookEngine&hooks,const native_source_stop::Session&stop,void*self,std::string*error){
 if(!runtime_slot_||*runtime_slot_!=runtime_||!construct_||!destroy_||!start_||!current_||current_()!=self||!stop.IsStopped(self))
  return Fail(error,"source-compilation-installation-needs-actual-runtime-stop");
 Service*expected=nullptr;
 if(!owner.compare_exchange_strong(expected,this,std::memory_order_acq_rel)&&expected!=this)
  return Fail(error,"source-compilation-observer-already-has-process-owner");
 struct Publication {
  Service&service;
  ~Publication(){if(!service.construct_token_&&!service.destroy_token_&&!service.start_token_){
   if(service.original_construct_||service.original_destroy_||service.original_start_)std::abort();
   owner.store(nullptr,std::memory_order_release);
  }}
 }publication{*this};
 if(!construct_token_){
  construct_token_=hooks.Install(reinterpret_cast<void*>(construct_),reinterpret_cast<void*>(Hooks::Construct),
    reinterpret_cast<void**>(&original_construct_),error);
  if(!construct_token_)return false;
  if(!original_construct_||original_construct_==construct_)std::abort();
 }
 if(!destroy_token_){
  destroy_token_=hooks.Install(reinterpret_cast<void*>(destroy_),reinterpret_cast<void*>(Hooks::Destroy),
    reinterpret_cast<void**>(&original_destroy_),error);
  if(!destroy_token_)return false;
  if(!original_destroy_||original_destroy_==destroy_)std::abort();
 }
 if(!start_token_){
  start_token_=hooks.Install(reinterpret_cast<void*>(start_),reinterpret_cast<void*>(Hooks::Start),
    reinterpret_cast<void**>(&original_start_),error);
  if(!start_token_)return false;
  if(!original_start_||original_start_==start_)std::abort();
 }
 if(!stop.IsStopped(self))std::abort();owner.store(this,std::memory_order_release);return true;
}
bool Service::Capture(const runnable_jni::Context&context,Receipt*out,const char**failure)const noexcept{
 if(failure)*failure=nullptr;if(out)*out={};
 if(!out||!runnable_jni::Bridge::BorrowOriginalScope(context)||!depth){
  if(failure)*failure="source-compilation-original-returned-scope-unobserved";return false;
 }
 const auto&frame=frames[depth-1];
 Receipt result;result.owner_=this;result.scope_=frame.scope;result.self_=frame.self;result.ticket_=frame.ticket;result.epoch_=frame.epoch;
 if(!Validate(result,context.self)){
  if(failure)*failure="source-compilation-original-scope-owner-or-restart-changed";return false;
 }
 *out=result;return true;
}
bool Service::Validate(const Receipt&receipt,void*self)const noexcept{
 if(receipt.owner_!=this||owner.load(std::memory_order_acquire)!=this||!construct_token_||!destroy_token_||!start_token_||
    !runtime_slot_||*runtime_slot_!=runtime_||!current_||current_()!=self||receipt.self_!=self||!depth||
    restarts.load(std::memory_order_acquire)||receipt.epoch_!=epoch.load(std::memory_order_acquire))return false;
 const auto&frame=frames[depth-1];
 return receipt.scope_&&frame.scope==receipt.scope_&&frame.self==self&&frame.ticket==receipt.ticket_&&frame.epoch==receipt.epoch_;
}
}

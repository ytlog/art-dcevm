#include "device_profile_fixture.h"
#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/abi/arm64/android13/suspend_deopt_art_abi.h"
#include "platform/android13/stack_access.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/symbols/android13/source_symbols.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <atomic>
#include <thread>
using namespace artdcevm::android13;
namespace {
int original_calls=0,finish_calls=0,context_calls=0,checks=0,header;
int target,other,runtime;
bool allowed=true,complete=false,recoverable=true;
art_method_abi::ArtMethod java{},native{},runtime_method{},unrelated{};
struct Frame {void* method;void** quick;const void* header;uintptr_t pc;};
std::vector<Frame> frames;
void* anchor_storage;
struct Shadow {Shadow* link;void* method;};
void Construct(void* raw,void* t,void* c,stack_abi::WalkKind k,bool check) {
  assert(check&&!c);auto* v=static_cast<stack_abi::Visitor*>(raw);
  std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(*v)-8);v->thread=t;v->kind=k;
}
void Walk(void* raw,bool transitions){assert(!transitions);auto* v=static_cast<stack_abi::Visitor*>(raw);
  assert(v->kind==stack_abi::WalkKind::kSkipInlined&&v->thread==&target);
  for(size_t i=0;i<frames.size();++i){v->depth=i;v->quick_frame=frames[i].quick;v->header=frames[i].header;v->pc=frames[i].pc;if(!v->VisitFrame())break;}}
void* Method(const void* v){return frames.at(static_cast<const stack_abi::Visitor*>(v)->depth).method;}
bool Async(const void* r,uintptr_t pc){assert(r==&runtime&&pc==0x12340);assert(EvolutionSuspendDeoptState(&target)==SuspendDeoptState::kClaimed);++checks;return allowed;}
void PushContext(void* thread,const suspend_abi::JValue& result,bool reference,suspend_abi::ThrowablePtr exception,bool from_code,suspend_abi::MethodType type){
  assert(thread==&target&&EvolutionSuspendDeoptState(thread)==SuspendDeoptState::kExecuting);
  assert(result.bits==0&&!reference&&exception.reference==0&&!from_code&&type==suspend_abi::MethodType::kDefault);
  assert(original_calls>context_calls);++context_calls;}
void Deoptimize(void* thread){assert(context_calls==finish_calls+1);++finish_calls;
  Shadow recovered{nullptr,&java},wrong{nullptr,&unrelated};
  assert(EvolutionSuspendDeoptInFlight());
  AcknowledgeEvolutionSuspendDeopt(&other,&recovered,false);
  AcknowledgeEvolutionSuspendDeopt(thread,nullptr,false);
  AcknowledgeEvolutionSuspendDeopt(thread,&wrong,false);
  AcknowledgeEvolutionSuspendDeopt(thread,&recovered,true);
  AcknowledgeEvolutionSuspendDeopt(thread,&recovered,false); // old quick anchor still present.
  assert(EvolutionSuspendDeoptState(thread)==SuspendDeoptState::kExecuting);
  CancelEvolutionSuspendDeopt(thread);ResetEvolutionSuspendDeopt();
  assert(EvolutionSuspendDeoptInFlight()&&!MarkEvolutionSuspendDeopt(&other));
  if(complete){frames.clear();AcknowledgeEvolutionSuspendDeopt(thread,&recovered,false);
    assert(EvolutionSuspendDeoptState(thread)==SuspendDeoptState::kCompleted);}
}
void Original(void*){++original_calls;}
class Hooks final:public artdcevm::HookEngine {
 public:bool missing=false;void(*proxy)(void*)=nullptr;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* symbol)override{
  if(missing)return nullptr;
#define FN(name,fn) if(std::strcmp(symbol,name)==0)return reinterpret_cast<void*>(fn)
  FN(::artdcevm::symbols::android13::kPushDeoptimizationContext,PushContext);FN(::artdcevm::symbols::android13::kDeoptimize,Deoptimize);FN(::artdcevm::symbols::android13::kTestSuspendFromCode,Original);FN(::artdcevm::symbols::android13::kIsAsyncDeoptimizeable,Async);
#undef FN
  if(std::strstr(symbol,"StackVisitorC2"))return reinterpret_cast<void*>(Construct);
  if(std::strstr(symbol,"WalkStack"))return reinterpret_cast<void*>(Walk);
  if(std::strstr(symbol,"GetMethodEv"))return reinterpret_cast<void*>(Method);
  return nullptr;}
 void* Install(void* target_fn,void* replacement,void** original,std::string*)override{*original=target_fn;proxy=reinterpret_cast<void(*)(void*)>(replacement);return this;}
 bool Remove(void*,std::string*)override{return true;}
};
void ResetFrames(){frames={{&runtime_method,&anchor_storage,&header,0},{&native,&anchor_storage,nullptr,0},{&java,&anchor_storage,&header,0x12340}};}
}
namespace artdcevm::android13 {void* RuntimeInstance(){return &runtime;}bool IsEvolutionPcRecoverable(void* thread,uintptr_t pc){assert(thread==&target&&pc==0x12340);return recoverable;}}
int main(){Hooks hooks;std::string error;java.dex_method_index=1;native=java;native.access_flags=0x100;runtime_method.dex_method_index=UINT32_MAX;
 assert(ConfigureStackAccess(hooks,MatchedProfileForTest(),&error));
 hooks.missing=true;assert(!InstallSuspendDeoptHook(hooks,&error));hooks.missing=false;assert(InstallSuspendDeoptHook(hooks,&error));
 hooks.proxy(&target);assert(!finish_calls);assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&other);assert(EvolutionSuspendDeoptPending(&target));
 allowed=false;ResetFrames();hooks.proxy(&target);assert(!context_calls&&!finish_calls&&checks==1);assert(EvolutionSuspendDeoptState(&target)==SuspendDeoptState::kRejected);assert(ReleaseEvolutionSuspendDeopt(&target));
 allowed=true;recoverable=false;ResetFrames();assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&target);assert(!context_calls&&EvolutionSuspendDeoptState(&target)==SuspendDeoptState::kRejected);assert(ReleaseEvolutionSuspendDeopt(&target));recoverable=true;
 ResetFrames();assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&target);assert(context_calls==1&&EvolutionSuspendDeoptInFlight());assert(!ReleaseEvolutionSuspendDeopt(&target));
 Shadow recovered{nullptr,&java};frames.clear();AcknowledgeEvolutionSuspendDeopt(&target,&recovered,false);assert(ReleaseEvolutionSuspendDeopt(&target));
 // Reuse with a different Java method: an old generation's shadow acknowledgement cannot match.
 unrelated.dex_method_index=2;ResetFrames();frames.back().method=&unrelated;assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&target);
 frames.clear();AcknowledgeEvolutionSuspendDeopt(&target,&recovered,false);assert(EvolutionSuspendDeoptInFlight());
 Shadow latest{nullptr,&unrelated};AcknowledgeEvolutionSuspendDeopt(&target,&latest,false);assert(ReleaseEvolutionSuspendDeopt(&target));
 complete=true;ResetFrames();assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&target);assert(!EvolutionSuspendDeoptInFlight());assert(ReleaseEvolutionSuspendDeopt(&target));
 assert(context_calls==3&&finish_calls==3&&EvolutionSuspendDeoptHits()==3);
 assert(MarkEvolutionSuspendDeopt(&target));CancelEvolutionSuspendDeopt(&target);assert(ReleaseEvolutionSuspendDeopt(&target));
 assert(MarkEvolutionSuspendDeopt(&target));ResetEvolutionSuspendDeopt();assert(!EvolutionSuspendDeoptPending(&target));
 // Ordinary interpreter deopts on unrelated ART threads must not read owner metadata.
 std::atomic<bool> stop{false};Shadow foreign{nullptr,&java};
 std::thread observer([&]{while(!stop.load(std::memory_order_acquire)) AcknowledgeEvolutionSuspendDeopt(&other,&foreign,false);});
 for(int i=0;i<1000;++i){ResetFrames();assert(MarkEvolutionSuspendDeopt(&target));hooks.proxy(&target);assert(ReleaseEvolutionSuspendDeopt(&target));}
 stop.store(true,std::memory_order_release);observer.join();
}

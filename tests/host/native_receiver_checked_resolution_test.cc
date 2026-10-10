#include "platform/common/native_receiver_checked_resolution.h"
#include <cassert>
#include <cstdio>
using namespace artdcevm::receiver_guard;
namespace {
struct State {void*self;void*linker;void*caller;void*method;uint32_t index,type;bool pending=false,fail_probe=false,change_thread=false,throw_original=false,null_original=false;int probes=0,calls=0,fail_probe_at=0;};
State state;
bool Current(void* owner,void* self)noexcept{return owner==&state&&self==state.self;}
bool Pending(void*owner,void*self,bool*out)noexcept {assert(Current(owner,self));++state.probes;if(state.fail_probe||state.probes==state.fail_probe_at)return false;*out=state.pending;return true;}
void* Original(void*linker,uint32_t index,void*caller,uint32_t type){
 assert(linker==state.linker&&caller==state.caller&&index==state.index&&type==state.type&&!state.pending);
 ++state.calls;if(state.change_thread)state.self=nullptr;if(state.throw_original)state.pending=true;
 return state.null_original?nullptr:state.method;
}
void Reset(){state={reinterpret_cast<void*>(0x100001001ULL),reinterpret_cast<void*>(0x200002002ULL),reinterpret_cast<void*>(0x300003004ULL),reinterpret_cast<void*>(0x400004004ULL),65535,0};}
}
int main(){
 CurrentCheckedAccess access{&state,Current,Pending,Original};
 constexpr uint32_t types[]{2,3,1,0,4};
 for(unsigned kind=0;kind<5;++kind){Reset();state.type=types[kind];void*self=state.self;
  auto invoke=[&]{return ResolveCurrentChecked(access,self,state.linker,state.index,state.caller,InvokeKind(kind));};
  auto result=invoke();assert(result.method==state.method&&!result.original_exception&&state.calls==1&&state.probes==2);
  Reset();state.type=types[kind];state.pending=true;result=invoke();assert(!result.method&&result.original_exception&&state.calls==0&&state.probes==1&&state.pending);
  Reset();state.type=types[kind];state.throw_original=true;state.null_original=true;result=invoke();assert(!result.method&&result.original_exception&&state.pending&&state.calls==1);
  Reset();state.type=types[kind];state.throw_original=true;result=invoke();assert(!result.method&&result.original_exception); // never publish target with pending
  Reset();state.type=types[kind];state.null_original=true;result=invoke();assert(!result.method&&!result.original_exception&&state.calls==1);
  Reset();state.type=types[kind];state.fail_probe=true;result=invoke();assert(!result.method&&!result.original_exception&&!state.calls);
  Reset();state.type=types[kind];state.fail_probe_at=2;result=invoke();assert(!result.method&&!result.original_exception&&state.calls==1&&state.probes==2);
  Reset();state.type=types[kind];state.change_thread=true;result=invoke();assert(!result.method&&!result.original_exception&&state.calls==1&&state.probes==1);
 }
 Reset();for(auto kind:{InvokeKind::Polymorphic,InvokeKind::Custom,InvokeKind::NonInvoke,InvokeKind(255)}){auto result=ResolveCurrentChecked(access,state.self,state.linker,state.index,state.caller,kind);assert(!result.method&&!result.original_exception&&!state.calls&&!state.probes);}
 auto result=ResolveCurrentChecked(access,reinterpret_cast<void*>(8),state.linker,state.index,state.caller,InvokeKind::Static);assert(!result.method&&!state.calls&&!state.probes);
 auto missing=access;missing.original=nullptr;result=ResolveCurrentChecked(missing,state.self,state.linker,state.index,state.caller,InvokeKind::Static);assert(!result.method&&!state.calls&&!state.probes);
 std::puts("Actual checked source leaf forwarding: all five InvokeType arguments, >32bit receivers/referrers/target, pending/null separation, wrong Thread and failed probes PASS");
}

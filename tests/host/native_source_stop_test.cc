#include "platform/common/native_source_stop.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
struct Thread {uint32_t flags=92u<<24;} self,foreign;
int heap,mutex;void* mutator=&mutex;
bool exclusive=false,gc=false,bad_transition=false,foreign_current=false;
int stops=0,resumes=0,locks=0,unlocks=0,gc_starts=0,gc_finishes=0;
a::SuspendConstruct stop_proxy=nullptr;a::SuspendDestroy resume_proxy=nullptr;
void* Current(){return foreign_current?&foreign:&self;}
void Native(void* p){assert(p==&self&&self.flags==0&&!exclusive);if(!bad_transition)self.flags=92u<<24;}
void Runnable(void* p){assert(p==&self&&self.flags==92u<<24&&!exclusive);self.flags=0;}
void Start(void* p,void* thread,int32_t cause,int32_t collector){assert(p==&heap&&thread==&self&&cause==8&&collector==1&&self.flags==92u<<24&&!gc&&!exclusive);gc=true;++gc_starts;}
void Finish(void* p,void* thread,int32_t type){assert(p==&heap&&thread==&self&&type==0&&gc&&!exclusive);gc=false;++gc_finishes;}
void Stop(void*,const char*,bool){assert(self.flags==92u<<24&&!exclusive&&stops==resumes);exclusive=true;++stops;}
void Resume(void*){assert(self.flags==92u<<24&&exclusive&&stops==resumes+1);exclusive=false;++resumes;}
void Lock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<24&&gc&&!exclusive&&stops==resumes+1);exclusive=true;++locks;}
void Unlock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<24&&gc&&exclusive);exclusive=false;++unlocks;}
class Hooks:public HookEngine {
 public:const char* missing=nullptr;int installations=0;bool fail_second=false;int removed=0;
 bool Initialize(std::string*)override{return true;}
 template<class T>static void* Fn(T f){return reinterpret_cast<void*>(f);}
 void* FindSymbol(const char*,const char* name)override {
  if(missing&&!std::strcmp(missing,name))return nullptr;
  if(!std::strcmp(name,a::kSuspendConstruct)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllC1EPKcb"))return Fn(stop_proxy?stop_proxy:Stop);
  if(!std::strcmp(name,a::kSuspendDestroy)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllD1Ev"))return Fn(resume_proxy?resume_proxy:Resume);
  if(!std::strcmp(name,abi::arm64::android13::kToNative))return Fn(Native);
  if(!std::strcmp(name,abi::arm64::android13::kToRunnable))return Fn(Runnable);
  if(!std::strcmp(name,s::kCurrent))return Fn(Current);
  if(!std::strcmp(name,s::kMutator))return &mutator;
  if(!std::strcmp(name,s::kStartGC))return Fn(Start);
  if(!std::strcmp(name,s::kFinishGC))return Fn(Finish);
  if(!std::strcmp(name,s::kLock))return Fn(Lock);
  if(!std::strcmp(name,s::kUnlock))return Fn(Unlock);
  return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override {
  assert(exclusive&&self.flags==92u<<24);++installations;
  if(fail_second&&installations==2)return nullptr;
  *original=target;
  if(target==Fn(Stop))stop_proxy=reinterpret_cast<a::SuspendConstruct>(proxy);
  else{assert(target==Fn(Resume));resume_proxy=reinterpret_cast<a::SuspendDestroy>(proxy);}
  return reinterpret_cast<void*>(uintptr_t(installations));
 }
 bool Remove(void*,std::string*)override{assert(exclusive);++removed;stop_proxy=nullptr;return true;}
};
thread_suspend_service::Service State(Hooks& h,thread_suspend_service::Declaration declaration=thread_suspend_service::Declaration::kModern24){
 thread_suspend_service::Service state;assert(thread_suspend_service::Service::Bind(declaration,[&](const char* n){return h.FindSymbol("libart.so",n);},&state));return state;
}
}
int main(int argc,char** argv){
 Hooks hooks;std::string error;
 if(argc==2&&!std::strcmp(argv[1],"install-rollback")){
  hooks.fail_second=true;assert(!native_source_stop::Install(hooks,&error));assert(hooks.removed==1&&!exclusive&&stops==resumes);std::cout<<"installation rollback PASS\n";return 0;
 }
 assert(native_source_stop::Install(hooks,&error));assert(!exclusive&&stops==resumes&&hooks.installations==2);
 auto state=State(hooks);native_source_stop::Service service;
 assert(!native_source_stop::Service::Bind(hooks,nullptr,state,&service,&error));
 assert(!native_source_stop::Service::Bind(hooks,&heap,State(hooks,thread_suspend_service::Declaration::kLegacy16),&service,&error));
 hooks.missing=s::kStartGC;assert(!native_source_stop::Service::Bind(hooks,&heap,state,&service,&error));hooks.missing=nullptr;
 assert(native_source_stop::Service::Bind(hooks,&heap,state,&service,&error));
 // No callback/phase boolean alone can grant any stop permission.
 native_source_stop::Session unopened(service);assert(!unopened.IsStopped(&self)&&!unopened.Reserved());
 assert(!unopened.Begin(&self,sizeof(self))); // ordinary Native JNI is not source Runnable
 assert(!unopened.Begin(&foreign,sizeof(foreign)));
 self.flags=0;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,0)&&!gc&&!exclusive);}
 bad_transition=true;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,sizeof(self))&&!gc&&!exclusive&&self.flags==0);}
 bad_transition=false;
 // A real refundable scan refusal unwinds the actual reservation, not just flags.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.IsStopped(&self)&&gc&&exclusive);
  assert(!owner.IsStopped(&foreign));foreign_current=true;assert(!owner.IsStopped(&self));foreign_current=false;
  native_source_stop::Session nested(service);const int count=gc_starts;assert(!nested.Begin(&self,sizeof(self))&&gc_starts==count);
 }
 assert(!gc&&!exclusive&&self.flags==0&&stops==resumes);
 // Original source continuation preserves actual global suspension and GC.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self)));
  const int captured=stops;assert(owner.ContinueSource()&&gc&&!exclusive&&owner.Reserved()&&!owner.IsStopped(&self)&&self.flags==0&&stops==captured&&stops==resumes+1);
  if(argc==2&&!std::strcmp(argv[1],"wrong-final")){Native(&self);unsigned char wrong;stop_proxy(&wrong,"unrelated stop",true);assert(false);}
  Native(&self);unsigned char original_source_carrier;
  stop_proxy(&original_source_carrier,s::kFinalCause,true);
  assert(owner.IsStopped(&self)&&stops==captured&&gc&&exclusive);
  resume_proxy(&original_source_carrier);
  assert(owner.Completed()&&!gc&&!exclusive&&stops==resumes&&self.flags==92u<<24);
  Runnable(&self);
 }
 // Abnormal source return still releases the owned reservation from Runnable.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.ContinueSource());}
 assert(!gc&&!exclusive&&self.flags==0&&stops==resumes&&gc_starts==gc_finishes&&locks==unlocks);
 std::cout<<"continuous original stop/GC ownership, transfer and rollback PASS\n";return 0;
}

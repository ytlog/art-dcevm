// Exercise actual production passthroughs and hook selection, not a second model.
#include "../../frameproof/src/main/cpp/frame_bootstrap.cc"
#include <cassert>
#include <iostream>
using namespace artdcevm;
using namespace artdcevm::frameproof;
namespace {
void* seen[3];uint32_t counts[2];bool boolean;int enumeration;unsigned calls=0,stops=0;
void NewEnqueue(void* a,void* b,void* c){seen[0]=a;seen[1]=b;seen[2]=c;++calls;}
bool OldEnqueue(void* a,void* b,void* c,uint32_t d,uint32_t e,bool f){NewEnqueue(a,b,c);counts[0]=d;counts[1]=e;boolean=f;return d==0x12345678u;}
void OldDone(void* a,void* b,void* c,bool d){NewEnqueue(a,b,c);boolean=d;}
void KindDone(void* a,void* b,void* c,int d){NewEnqueue(a,b,c);enumeration=d;}
void NewDone(void* a,void* b,void* c){NewEnqueue(a,b,c);}
void Collect(void* a,bool b,int c){seen[0]=a;boolean=b;enumeration=c;++calls;}
void Suspend(void*,const char*,bool long_stop){assert(!long_stop);++stops;}
void Resume(void*){assert(stops==1);--stops;}
struct Hooks:HookEngine {
 unsigned family=14;bool modern=true,missing=false;std::array<void*,3> replacements{};unsigned installed_count=0;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendConstruct))return reinterpret_cast<void*>(Suspend);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);
  if(!missing&&!std::strcmp(name,kEnqueue)&&modern)return reinterpret_cast<void*>(NewEnqueue);
  if(!missing&&!std::strcmp(name,kLegacyEnqueue)&&!modern)return reinterpret_cast<void*>(OldEnqueue);
  if(!std::strcmp(name,kDone11)&&family==11)return reinterpret_cast<void*>(OldDone);
  if(!std::strcmp(name,kDone12)&&family==12)return reinterpret_cast<void*>(KindDone);
  if(!std::strcmp(name,kDone14)&&family==14)return reinterpret_cast<void*>(NewDone);
  if(!std::strcmp(name,kHeapCollect))return reinterpret_cast<void*>(Collect);
  return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override{
  assert(stops==1&&installed_count<3);*original=target;replacements[installed_count++]=replacement;return target;
 }
 bool Remove(void*,std::string*)override{return false;}
};
void Reset(){
 attempted=installed=false;enqueue=nullptr;legacy_enqueue=nullptr;done11=nullptr;done12=nullptr;done14=nullptr;heap_collect=nullptr;
 observation.expected=nullptr;observation.jit=nullptr;observation.cache=nullptr;observation.heap=nullptr;observation.gc_active=false;observation.conflict=false;
 observation.enqueues=0;observation.completions=0;observation.collections=0;
}
}
int main(){
 int receiver=1,method=2,thread=3,other=4,cache_receiver=5,heap_receiver=6;
 for(unsigned family:{11u,12u,14u})for(bool modern:{false,true}){
  Reset();Hooks hooks;hooks.family=family;hooks.modern=modern;std::string error;assert(Install(hooks,&error)&&hooks.installed_count==3&&stops==0);
  observation.expected=&method;calls=0;
  if(modern){auto proxy=reinterpret_cast<Enqueue>(hooks.replacements[0]);proxy(&receiver,&other,&thread);assert(observation.enqueues==0);proxy(&receiver,&method,&thread);}
  else {auto proxy=reinterpret_cast<LegacyEnqueue>(hooks.replacements[0]);assert(!proxy(&receiver,&thread,&other,0xfedcba98u,0x87654321u,true));assert(observation.enqueues==0);assert(!proxy(&receiver,&thread,&method,0xfedcba98u,0x87654321u,true));assert(counts[0]==0xfedcba98u&&counts[1]==0x87654321u&&boolean);assert(proxy(&receiver,&thread,&other,0x12345678u,0xffffffffu,false));assert(counts[1]==0xffffffffu&&!boolean);}
  assert(calls==(modern?2u:3u)&&observation.jit==&receiver&&observation.enqueues==1);
  if(family==11){auto proxy=reinterpret_cast<Done11>(hooks.replacements[1]);proxy(&cache_receiver,&other,&thread,false);assert(observation.completions==0);proxy(&cache_receiver,&method,&thread,true);assert(boolean);}
  else if(family==12){auto proxy=reinterpret_cast<Done12>(hooks.replacements[1]);proxy(&cache_receiver,&other,&thread,3);assert(observation.completions==0);proxy(&cache_receiver,&method,&thread,2);assert(enumeration==2);}
  else {auto proxy=reinterpret_cast<Done14>(hooks.replacements[1]);proxy(&cache_receiver,&other,&thread);assert(observation.completions==0);proxy(&cache_receiver,&method,&thread);}
  assert(seen[0]==&cache_receiver&&seen[1]==&method&&seen[2]==&thread&&observation.cache==&cache_receiver&&observation.completions==1);
  auto gc=reinterpret_cast<HeapCollect>(hooks.replacements[2]);gc(&heap_receiver,false,7);assert(observation.collections==0);observation.gc_active=true;gc(&heap_receiver,true,9);
  assert(boolean&&enumeration==9&&observation.heap==&heap_receiver&&observation.collections==1&&observation.Complete());
  observation.Enqueued(&other,&method);assert(!observation.Complete()&&observation.conflict);
  observation.expected=nullptr;unsigned complete=observation.completions;observation.Completed(&other,&method);assert(observation.completions==complete);
 }
 Reset();Hooks missing;missing.missing=true;std::string error;assert(!Install(missing,&error)&&missing.installed_count==0&&!attempted&&!error.empty());
 runtime_services::View a;a.class_linker=&receiver;a.jni_id_manager=&method;a.jit=&thread;a.jit_code_cache=&cache_receiver;a.thread_list=&other;a.heap=&heap_receiver;a.java_vm=&a;
 assert(Same(a,a));
 for(auto member:{&runtime_services::View::class_linker,&runtime_services::View::jni_id_manager,&runtime_services::View::jit,&runtime_services::View::jit_code_cache,&runtime_services::View::thread_list,&runtime_services::View::heap,&runtime_services::View::java_vm}){
  auto b=a;b.*member=nullptr;assert(!Same(a,b)&&!Same(b,a));b.*member=&error;assert(!Same(a,b));
 }
 std::cout<<"Production observer ABI forwarding (legacy bool/u32, Done11 bool, Done12 enum, Done14 void, GC bool/enum), own-method filtering, asynchronous conflicts and all seven Runtime receiver gates passed\n";
}

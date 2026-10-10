// Exercise the actual production installer and observers, not a copied selector.
#include "platform/common/runtime_service_calibration.cc"
#include <cassert>
#include <cstring>
#include <iostream>
namespace artdcevm::runnable_executor {
bool Initialize(HookEngine&,JNIEnv*,std::string*) {return false;}
runnable_jni::Result With(JNIEnv*,jobject,runnable_jni::Operation,void*) {
  return runnable_jni::Result::kUnavailable;
}
}
namespace artdcevm::runtime_receiver {
bool Calibrate(HookEngine&,JNIEnv*,Receivers*,std::string*) {return false;}
}
namespace artdcevm::runtime_instance {
bool Calibrate(HookEngine&,JavaVM*,void**,std::string*) {return false;}
}
using namespace artdcevm;
using abi::arm64::android_common::ListenerType;
namespace {
// Independent exact ELF catalog: Pixel 9 Android 14 exports enum overload;
// fixed Android 11/12 and Android 15 source declarations supply other overloads.
constexpr const char* names[]={
 "_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjNS1_12ListenerTypeE",
 "_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb",
 "_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj"};
constexpr const char* constructor="_ZN3art16ScopedSuspendAllC2EPKcb";
constexpr const char* destructor="_ZN3art16ScopedSuspendAllD2Ev";
int calls=0,constructs=0,destroys=0,depth=0,installations=0,seen_kind=-1;
void* seen_receiver=nullptr;void* seen_listener=nullptr;uint32_t seen_events=0;
void Record(void* receiver,void* listener,uint32_t events,int kind) {
 ++calls;seen_receiver=receiver;seen_listener=listener;seen_events=events;seen_kind=kind;
}
void Typed(void* receiver,void* listener,uint32_t events,ListenerType kind) {
 Record(receiver,listener,events,static_cast<int>(kind));
}
void Bool(void* receiver,void* listener,uint32_t events,bool trace) {
 Record(receiver,listener,events,trace?1:0);
}
void Legacy(void* receiver,void* listener,uint32_t events) {
 Record(receiver,listener,events,-1);
}
void Construct(void*,const char*,bool long_suspend) {
 assert(!depth&&!long_suspend);++constructs;++depth;
}
void Destroy(void*) {assert(depth==1);++destroys;--depth;}
struct Hooks:HookEngine {
 std::string mode;void* callback=nullptr;int selected=-1;
 explicit Hooks(const char* value):mode(value) {}
 bool Initialize(std::string*)override {return true;}
 void* FindSymbol(const char* library,const char* name)override {
  assert(!std::strcmp(library,"libart.so"));
  for(int i=0;i<3;++i)if(!std::strcmp(name,names[i])) {
   bool available=mode=="all"||mode=="enum"||mode=="missing-construct"||
     mode=="missing-destroy"||mode=="install-failure";
   if(i==1)available=mode=="bool"||mode=="all";
   if(i==2)available=mode=="legacy"||mode=="all";
   if(!available)return nullptr;
   selected=i;
   return i==0?reinterpret_cast<void*>(Typed):i==1?reinterpret_cast<void*>(Bool):reinterpret_cast<void*>(Legacy);
  }
  if(!std::strcmp(name,constructor))return mode=="missing-construct"?nullptr:reinterpret_cast<void*>(Construct);
  if(!std::strcmp(name,destructor))return mode=="missing-destroy"?nullptr:reinterpret_cast<void*>(Destroy);
  assert(false&&"Unrecognized named ABI");return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override {
  assert(depth==1);++installations;
  if(mode=="install-failure")return nullptr;
  callback=proxy;*original=target;return target;
 }
 bool Remove(void*,std::string*)override {assert(false);return false;}
 void Invoke(void* receiver,void* listener,uint32_t events,int kind) {
  if(selected==0)reinterpret_cast<void (*)(void*,void*,uint32_t,ListenerType)>(callback)(receiver,listener,events,static_cast<ListenerType>(kind));
  else if(selected==1)reinterpret_cast<void (*)(void*,void*,uint32_t,bool)>(callback)(receiver,listener,events,kind!=0);
  else reinterpret_cast<void (*)(void*,void*,uint32_t)>(callback)(receiver,listener,events);
 }
};
}
int main(int argc,char** argv) {
 assert(argc==2);Hooks hooks(argv[1]);std::string error;
 using namespace artdcevm::runtime_services;
 bool ok=Install(hooks,&error);
 if(hooks.mode=="missing-listener"||hooks.mode=="missing-construct"||hooks.mode=="missing-destroy") {
  assert(!ok&&constructs==0&&destroys==0&&installations==0&&!attempted);
  const char* message=hooks.mode=="missing-listener"?"AddListener":hooks.mode=="missing-construct"?"constructor":"destructor";
  assert(error.find(message)!=std::string::npos);
  hooks.mode="enum";assert(Install(hooks,&error)); // Missing capability is retryable.
 } else if(hooks.mode=="install-failure") {
  assert(!ok&&constructs==1&&destroys==1&&depth==0&&attempted);
  hooks.mode="enum";assert(!Install(hooks,&error)&&installations==1);
  assert(error.find("restart process")!=std::string::npos);
  std::cout<<"Failed installation releases suspension and rejects unsafe retry\n";return 0;
 } else assert(ok);
 assert(constructs==1&&destroys==1&&depth==0&&installations==1);
 assert(Install(hooks,&error)&&installations==1); // One process-lifetime owner.
 if(hooks.mode=="all")assert(hooks.selected==0); // Prefer accurate modern signature.
 void* receiver=reinterpret_cast<void*>(0x1230);void* listener=reinterpret_cast<void*>(0x2340);
 Observation observation;active=&observation;
 // Source enum values 0,1,2 are all passed through unchanged, not cast to bool.
 const int kinds=hooks.selected==0?3:hooks.selected==1?2:1;
 for(int kind=0;kind<kinds;++kind) {
  hooks.Invoke(receiver,listener,0x800000a5u,kind);
  assert(seen_receiver==receiver&&seen_listener==listener&&seen_events==0x800000a5u);
  assert(seen_kind==(hooks.selected==2?-1:kind));
 }
 assert(observation.instrumentation==receiver&&!observation.conflict);
 hooks.Invoke(reinterpret_cast<void*>(0x3450),listener,7,0);
 assert(observation.conflict); // Multiple receivers never grant a proof.
 Observation missing;active=&missing;hooks.Invoke(nullptr,listener,7,0);
 assert(missing.conflict&&!missing.instrumentation);
 active=nullptr;int before=calls;hooks.Invoke(receiver,listener,7,0);
 assert(calls==before+1); // Callback outside a proof remains original passthrough.
 std::cout<<"Named listener observer "<<argv[1]<<": ABI forwarding, receiver proof, lifetime passed\n";
}

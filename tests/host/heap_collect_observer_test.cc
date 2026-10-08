#include "platform/common/heap_collect_observer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
using Cause=heap_collect_observer::Cause;
using Function=void (*)(void*,bool,Cause);
namespace {
const char* mode="success";
Function proxy=nullptr;
int suspended=0,installs=0,original_calls=0,first_calls=0,second_calls=0;
void* actual=reinterpret_cast<void*>(0x1230);
bool forwarding=false;
void Original(void* receiver,bool clear,Cause cause) {
 assert(receiver==actual&&clear&&cause==art::gc::kGcCauseRunEmptyCheckpoint);++original_calls;
 if(forwarding)throw std::runtime_error("original exception");
}
void First(void* receiver,bool clear,Cause cause){assert(receiver==actual&&clear&&cause==art::gc::kGcCauseRunEmptyCheckpoint);++first_calls;}
void Second(void* receiver,bool clear,Cause cause){assert(receiver==actual&&clear&&cause==art::gc::kGcCauseRunEmptyCheckpoint);++second_calls;}
void Construct(void*,const char*,bool slow){assert(!suspended&&!slow);++suspended;}
void Destroy(void*){assert(suspended==1);--suspended;}
class Hooks:public HookEngine {
 public:
 bool Initialize(std::string*)override{assert(false);return false;}
 void* FindSymbol(const char*,const char* name)override{
  if(!std::strcmp(name,"_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE"))return !std::strcmp(mode,"missing")?nullptr:reinterpret_cast<void*>(Original);
  if(std::strstr(name,"C2"))return !std::strcmp(mode,"ctor")?nullptr:reinterpret_cast<void*>(Construct);
  if(std::strstr(name,"D2"))return !std::strcmp(mode,"dtor")?nullptr:reinterpret_cast<void*>(Destroy);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override{
  assert(suspended==1&&target==reinterpret_cast<void*>(Original)&&++installs==1);
  if(!std::strcmp(mode,"install-failed"))return nullptr;
  proxy=reinterpret_cast<Function>(replacement);*original=target;return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
}
int main(int argc,char** argv) {
 if(argc>1)mode=argv[1];const char* requested=mode;Hooks hooks;std::string error;
 assert(!heap_collect_observer::Install(hooks,nullptr,&error)&&!error.empty()&&installs==0);
 bool ok=heap_collect_observer::Install(hooks,First,&error);
 if(std::strcmp(mode,"success")) {
  assert(!ok&&!error.empty()&&!suspended&&!proxy);
  if(!std::strcmp(mode,"install-failed")) {
   assert(!heap_collect_observer::Install(hooks,Second,&error)&&installs==1);
  }else {
   // Missing declarations do not poison later actual capability availability.
   mode="success";assert(heap_collect_observer::Install(hooks,First,&error));
   assert(installs==1);
  }
 }else {
  assert(ok&&error.empty());
  assert(heap_collect_observer::Install(hooks,First,&error));
  assert(heap_collect_observer::Install(hooks,Second,&error));
  assert(heap_collect_observer::Install(hooks,Second,&error)&&installs==1);
  proxy(actual,true,art::gc::kGcCauseRunEmptyCheckpoint);
  assert(original_calls==1&&first_calls==1&&second_calls==1);
  forwarding=true;
  try{proxy(actual,true,art::gc::kGcCauseRunEmptyCheckpoint);assert(false);}catch(const std::runtime_error& e){assert(!std::strcmp(e.what(),"original exception"));}
  assert(original_calls==2&&first_calls==2&&second_calls==2);
 }
 assert(!suspended);std::cout<<"Shared Heap collector owner "<<requested<<": passed\n";
}

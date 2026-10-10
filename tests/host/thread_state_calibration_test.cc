#include "platform/common/thread_state_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
namespace {
uint32_t word=0;int calls=0,reads=0,transitions=0,suspends=0;
unsigned shift=24;bool pair=true,bridge=true,pending=false,mapping=true,oracle=true;
bool throws=false,missing_current=false,missing_scope=false;
bool pending_after=false,current_after=false,returned=false;
bool vm_bad=false,ref_bad=false,self_bad=false,context_bad=false,run_bad=false,restore_bad=false,ambiguous=false,repeated=false,bridge_failure=false;
JavaVM vm{},foreign_vm{};jobject carrier=reinterpret_cast<jobject>(0x1000);
void* Current(){++calls;return current_after&&returned?nullptr:&word;}
void Native(void*){++transitions;}
void Runnable(void*){++transitions;}
void Construct(void*,const char*,bool){++suspends;}
void Destroy(void*){++suspends;}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  using namespace abi::arm64;
  if(!std::strcmp(name,android11::kCurrentThread))return missing_current?nullptr:reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,::artdcevm::abi::common::kSuspendConstruct))return missing_scope?nullptr:reinterpret_cast<void*>(Construct);
  if(!std::strcmp(name,::artdcevm::abi::common::kSuspendDestroy))return reinterpret_cast<void*>(Destroy);
  if(pair&&!std::strcmp(name,android13::kToNative))return reinterpret_cast<void*>(Native);
  if(pair&&!std::strcmp(name,android13::kToRunnable))return reinterpret_cast<void*>(Runnable);
  return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jint GetVM(JNIEnv*,JavaVM** out){*out=vm_bad?&foreign_vm:&vm;return JNI_OK;}
jboolean Exception(JNIEnv*){return pending?JNI_TRUE:JNI_FALSE;}
jobjectRefType RefType(JNIEnv*,jobject ref){assert(ref==carrier);return ref_bad?JNIWeakGlobalRefType:JNIGlobalRefType;}
JNINativeInterface_ functions{};JNIEnv env{&functions};
void Reset(unsigned bits=24){shift=bits;word=(92u<<shift)|5u;calls=reads=transitions=suspends=0;pair=bridge=mapping=oracle=true;throws=missing_current=missing_scope=pending_after=current_after=returned=pending=vm_bad=ref_bad=self_bad=context_bad=run_bad=restore_bad=ambiguous=repeated=bridge_failure=false;}
}
namespace artdcevm::method_identity {
bool ReadableMappingExtent(const void* self,size_t* extent){assert(self==&word);++reads;if(!mapping)return false;*extent=4;
 // Contradictory source observations must not select a candidate: emulate
 // an unstable/corrupt state word between the two declaration reads.
 if(ambiguous&&reads==1)word=92u<<16;
 if(ambiguous&&reads==2)word=92u<<24;
 return true;
}
}
namespace artdcevm::runnable_executor {
bool Available(){return bridge;}
bool DecodeStrongGlobal(const runnable_jni::Context& scope,jobject ref,void** out){assert(scope.env==&env&&ref==carrier);*out=oracle?reinterpret_cast<void*>(0x2000):nullptr;return oracle;}
runnable_jni::Result With(JNIEnv* jni,jobject ref,runnable_jni::Operation op,void* data){
 assert(jni==&env&&ref==carrier);word=run_bad?1u<<shift:(shift==16?67u<<16:0u)|5u;
 runnable_jni::Context scope{self_bad?nullptr:&word,&env,context_bad?&foreign_vm:&vm};
 bool ok=op(scope,data);if(repeated)ok=op(scope,data)&&ok;
 word=restore_bad?0u:(92u<<shift)|5u;pending=pending_after;returned=true;
 if(throws)throw std::runtime_error("restored bridge failure");
 return ok&&!bridge_failure?runnable_jni::Result::kSuccess:runnable_jni::Result::kContextMismatch;
}
}
int main(){
 functions.GetJavaVM=GetVM;functions.ExceptionCheck=Exception;functions.GetObjectRefType=RefType;
 Hooks hooks;thread_suspend_service::Service service;std::string error;
 for(unsigned bits:{16u,24u}){
  Reset(bits);assert(thread_state_calibration::Calibrate(hooks,&env,carrier,&service,&error));
  assert(service.CanTransitionRunnable()==(bits==24));assert(transitions==0&&suspends==0);
  uint32_t state=0;assert(service.State(&word,4,&state)&&state==92);
 }
 Reset();pair=false;assert(thread_state_calibration::Calibrate(hooks,&env,carrier,&service,&error));assert(!service.CanTransitionRunnable());
 for(int test=0;test<17;++test){Reset();
  switch(test){case 0:vm_bad=true;break;case 1:ref_bad=true;break;case 2:pending=true;break;case 3:bridge=false;break;case 4:mapping=false;break;case 5:self_bad=true;break;case 6:context_bad=true;break;case 7:run_bad=true;break;case 8:restore_bad=true;break;case 9:ambiguous=true;break;case 10:oracle=false;break;case 11:repeated=true;break;case 12:pending_after=true;break;case 13:current_after=true;break;case 14:throws=true;break;case 15:missing_current=true;break;case 16:missing_scope=true;break;}
  assert(!thread_state_calibration::Calibrate(hooks,&env,carrier,&service,&error));assert(!error.empty());assert(!service.CanTransitionRunnable());uint32_t state;assert(!service.State(&word,4,&state));assert(transitions==0&&suspends==0);
 }
 Reset();word=0;assert(!thread_state_calibration::Calibrate(hooks,&env,carrier,&service,&error));
 Reset();bridge_failure=true;assert(!thread_state_calibration::Calibrate(hooks,&env,carrier,&service,&error));
 std::cout<<"PASS actual thread-state calibration TU: two declarations, passive missing pair, contexts/restoration/ambiguity refusal\n";
}

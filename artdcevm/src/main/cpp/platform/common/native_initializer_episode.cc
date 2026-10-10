#include "platform/common/native_initializer_episode.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android11/class_initialization.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>
namespace artdcevm::native_initializer_episode {
namespace {
using namespace abi::arm64::android11;
struct Trial {
 jobject fixture=nullptr;
 bool failure=false;
 std::atomic<void*> owner{nullptr},waiter{nullptr};
 std::atomic<uint32_t> tid{0},init_enter{0},init_exit{0},wait_enter{0},wait_exit{0};
 std::atomic<bool> valid{true},body_ready{false};
};
struct Installed {
 const Service* owner=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 const source_handle_roots::Service* roots=nullptr;
 JavaVM* vm=nullptr;jobject anchor=nullptr;
 void*(*current)()=nullptr;
 InitializeClass initialize=nullptr;WaitForInitializeClass wait=nullptr;
 bool installed=false;
};
Installed installed;
std::atomic<Trial*> active{nullptr};
std::atomic<size_t> readers{0};
struct BorrowTrial {
 Trial* value=nullptr;bool held=true;
 BorrowTrial(){readers.fetch_add(1,std::memory_order_seq_cst);value=active.load(std::memory_order_seq_cst);}
 void Release(){if(held){held=false;readers.fetch_sub(1,std::memory_order_seq_cst);}}
 ~BorrowTrial(){Release();}
};
std::atomic<bool> ready{false};
struct BodyRequest{Trial* trial;JNIEnv* env;void* self;uint32_t tid;};
bool StateAt(const void*,uint32_t*,uint32_t*);
}

bool CaptureBody(const runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<BodyRequest*>(raw);void* klass=nullptr;uint32_t status=0,tid=0;
 return context.env==request.env&&context.self==request.self&&context.vm==installed.vm&&
     installed.roots->BorrowOwnedStrongGlobal(context,request.trial->fixture,&klass)&&
     StateAt(klass,&status,&tid)&&status==13&&tid==request.tid;
}
static_assert(std::is_same<decltype(&CaptureBody),runnable_jni::Operation>::value,"Actual source callback ABI must match");

namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool StateAt(const void* klass,uint32_t* status,uint32_t* tid){
 const bool legacy=installed.metadata->StorageModel()==modern_field_metadata::Model::kSplit;
 size_t size=0;class_metadata::State value{};
 if(!modern_field_metadata::ReadMappingExtent(nullptr,klass,&size)||
    !class_metadata::ReadState(legacy?class_metadata::kLegacy:class_metadata::k17,klass,size,&value))return false;
 const size_t offset=legacy?offsetof(class_metadata::Class11,clinit_thread_id):offsetof(class_metadata::Class17,clinit_thread_id_or_hash);
 *status=value.status;*tid=__atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(klass)+offset),__ATOMIC_SEQ_CST);return true;
}
// All JNI VM lookup occurs before borrowing any native managed reference. Both
// Handle and strong global are then read in the same source Runnable interval.
bool ReadHandle(Trial& trial,void* self,ClassHandle handle,uint32_t* status,uint32_t* tid)noexcept{
 try{
 JNIEnv* env=nullptr;
 if(!self||self!=installed.current()||!handle.slot||
    installed.vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)return false;
 runnable_jni::Context context{self,env,installed.vm};void* actual=nullptr,*expected=nullptr;
 return installed.roots->BorrowHandle(context,handle.slot,&actual)&&actual&&
     installed.roots->BorrowOwnedStrongGlobal(context,trial.fixture,&expected)&&actual==expected&&
     StateAt(actual,status,tid);
 }catch(...){return false;}
}
bool HookInitialize(void* linker,void* self,ClassHandle handle,bool statics,bool parents){
 BorrowTrial borrowed;Trial* trial=borrowed.value;uint32_t status=0,tid=0;
 const bool matched=trial&&ReadHandle(*trial,self,handle,&status,&tid);
 if(!matched)borrowed.Release();
 if(matched){trial->init_enter.fetch_add(1,std::memory_order_relaxed);
  if(!statics||!parents)trial->valid.store(false,std::memory_order_release);}
 const bool result=installed.initialize(linker,self,handle,statics,parents);
 if(matched){uint32_t completed=0,unused=0;
  if(!ReadHandle(*trial,self,handle,&completed,&unused)||
     result==trial->failure||(trial->failure?completed!=3:(completed<14||completed>15)))
   trial->valid.store(false,std::memory_order_release);
  trial->init_exit.fetch_add(1,std::memory_order_release);
 }
 return result;
}
bool HookWait(void* linker,ClassHandle handle,void* self,void* lock){
 BorrowTrial borrowed;Trial* trial=borrowed.value;uint32_t status=0,tid=0;
 const bool matched=trial&&ReadHandle(*trial,self,handle,&status,&tid);
 if(!matched)borrowed.Release();
 if(matched){
  if(!lock||!trial->body_ready.load(std::memory_order_acquire)||
     self!=trial->waiter.load(std::memory_order_acquire)||
     self==trial->owner.load(std::memory_order_acquire)||status!=13||
     tid!=trial->tid.load(std::memory_order_acquire)||
     trial->wait_enter.fetch_add(1,std::memory_order_acq_rel)!=0)
   trial->valid.store(false,std::memory_order_release);
 }
 // Original source owns ObjectLock::WaitIgnoringInterrupts, status rereads,
 // NCDFE synthesis and the eventual return. The observer never simulates it.
 const bool result=installed.wait(linker,handle,self,lock);
 if(matched){uint32_t completed=0,unused=0;
  if(!ReadHandle(*trial,self,handle,&completed,&unused)||
     result==trial->failure||(trial->failure?completed!=3:(completed<14||completed>15)))
   trial->valid.store(false,std::memory_order_release);
  trial->wait_exit.fetch_add(1,std::memory_order_release);
 }
 return result;
}
// Waiting here occurs only in ordinary Native JNI, with no borrowed raw roots.
// Bounded failure lets the original body throw and wake its source waiter.
bool Await(const std::atomic<bool>& value){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(!value.load(std::memory_order_acquire)){
  if(std::chrono::steady_clock::now()>=deadline)return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }return true;
}
jboolean Body(JNIEnv* env,jclass,jclass klass,jint public_tid){
 BorrowTrial borrowed;Trial* trial=borrowed.value;
 if(!trial||public_tid<=0||!env->IsSameObject(klass,trial->fixture)||env->ExceptionCheck())return JNI_FALSE;
 void* self=installed.current();void* empty=nullptr;
 runnable_jni::Context native{self,env,installed.vm};
 if(!installed.roots->OrdinaryNative(native)||!trial->owner.compare_exchange_strong(empty,self))return JNI_FALSE;
 BodyRequest request{trial,env,self,uint32_t(public_tid)};
 if(runnable_executor::With(env,installed.anchor,CaptureBody,&request)!=runnable_jni::Result::kSuccess)return JNI_FALSE;
 trial->tid.store(uint32_t(public_tid),std::memory_order_relaxed);
 trial->body_ready.store(true,std::memory_order_release);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(trial->wait_enter.load(std::memory_order_acquire)==0){
  if(std::chrono::steady_clock::now()>=deadline)return JNI_FALSE;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return trial->valid.load(std::memory_order_acquire)?JNI_TRUE:JNI_FALSE;
}
jboolean Waiter(JNIEnv* env,jclass){
 BorrowTrial borrowed;Trial* trial=borrowed.value;
 if(!trial||!Await(trial->body_ready))return JNI_FALSE;
 void* self=installed.current();void* empty=nullptr;
 if(!installed.roots->OrdinaryNative({self,env,installed.vm})||
    self==trial->owner.load(std::memory_order_acquire)||
    !trial->waiter.compare_exchange_strong(empty,self))return JNI_FALSE;
 return JNI_TRUE;
}
}
bool Service::Calibrate(HookEngine& hooks,JNIEnv* env,jobject anchor,
    const modern_field_metadata::FieldMetadata& metadata,const source_handle_roots::Service& roots,
    std::string* error){
 if(installed.owner){
  if(installed.owner!=this||installed.metadata!=&metadata||installed.roots!=&roots)
   return Fail(error,"Initialization observers already belong to a different live owner");
  if(ready.load(std::memory_order_acquire)){if(error)error->clear();return true;}
  return Fail(error,"Private initialization episode already consumed without complete source evidence");
 }
 if(!env||!anchor||env->ExceptionCheck()||env->GetObjectRefType(anchor)!=JNIGlobalRefType||
    env->ExceptionCheck()||!metadata.Calibrated()||!roots.Calibrated()||!runnable_executor::Available())
  return Fail(error,"Initialization observation requires genuine field/root/Runnable owners");
 Installed candidate;candidate.owner=this;candidate.metadata=&metadata;candidate.roots=&roots;
 candidate.current=reinterpret_cast<void*(*)()>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
 void* initialize=hooks.FindSymbol("libart.so",kInitializeClass);
 void* wait=hooks.FindSymbol("libart.so",kWaitForInitializeClass);
 using namespace abi::common;
 auto suspend=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto resume=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!candidate.current||!initialize||!wait||initialize==wait||!suspend||!resume||
    env->GetJavaVM(&candidate.vm)!=JNI_OK||!candidate.vm||
    !roots.OrdinaryNative({candidate.current(),env,candidate.vm}))
  return Fail(error,"Original initialization/wait declarations or ordinary Native owner unavailable");
 candidate.anchor=env->NewGlobalRef(anchor);if(!candidate.anchor||env->ExceptionCheck())return false;
 void* tokens[2]{};bool ok=false;
 {
  SuspendAll stop{};suspend(&stop,"artdcevm source initialization observers",false);
  installed=candidate;
  tokens[0]=hooks.Install(initialize,reinterpret_cast<void*>(HookInitialize),reinterpret_cast<void**>(&installed.initialize),error);
  if(tokens[0])tokens[1]=hooks.Install(wait,reinterpret_cast<void*>(HookWait),reinterpret_cast<void**>(&installed.wait),error);
  ok=tokens[0]&&tokens[1];
  if(!ok){bool removed=true;for(int i=1;i>=0;--i)if(tokens[i]){std::string rollback;if(!hooks.Remove(tokens[i],&rollback))removed=false;}
   if(removed)installed={};}
  resume(&stop);
 }
 if(!ok){if(!installed.owner)env->DeleteGlobalRef(candidate.anchor);return false;}
 installed.installed=true;
 jclass oracle=env->FindClass("com/github/ytlog/artdcevm/internal/InitializerEpisodeOracle");
 if(!oracle||env->ExceptionCheck())return Fail(error,"Private original initialization episode unavailable");
 const JNINativeMethod methods[]{
  {const_cast<char*>("body"),const_cast<char*>("(Ljava/lang/Class;I)Z"),reinterpret_cast<void*>(Body)},
  {const_cast<char*>("waiter"),const_cast<char*>("()Z"),reinterpret_cast<void*>(Waiter)}};
 jmethodID type=env->GetStaticMethodID(oracle,"type","(Z)Ljava/lang/Class;");
 jmethodID run=env->GetStaticMethodID(oracle,"run","(Z)I");
 ok=type&&run&&!env->ExceptionCheck()&&env->RegisterNatives(oracle,methods,2)==JNI_OK;
 for(bool failure:{false,true}){
  if(!ok)break;
  jobject local=env->CallStaticObjectMethod(oracle,type,jboolean(failure));
  jobject strong=local&&!env->ExceptionCheck()?env->NewGlobalRef(local):nullptr;
  if(!strong||env->ExceptionCheck()){if(local)env->DeleteLocalRef(local);ok=false;break;}
  Trial trial;trial.fixture=strong;trial.failure=failure;active.store(&trial,std::memory_order_seq_cst);
  const jint result=env->CallStaticIntMethod(oracle,run,jboolean(failure));
  active.store(nullptr,std::memory_order_seq_cst);
  // Also drain an unrelated source hook which may have loaded this epoch before
  // it was unpublished. No Class/global/stack Trial is freed while it is read.
  while(readers.load(std::memory_order_seq_cst)!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ok=!env->ExceptionCheck()&&result==1&&trial.valid.load(std::memory_order_acquire)&&
      trial.owner.load()!=nullptr&&trial.waiter.load()!=nullptr&&trial.owner.load()!=trial.waiter.load()&&
      trial.init_enter.load()==2&&trial.init_exit.load()==2&&trial.wait_enter.load()==1&&trial.wait_exit.load()==1;
  env->DeleteGlobalRef(strong);env->DeleteLocalRef(local);
 }
 env->DeleteLocalRef(oracle);
 if(!ok)return Fail(error,"Original initialization/wait roots, return, normal/error completion and Java body oracle disagree");
 ready.store(true,std::memory_order_release);if(error)error->clear();return true;
}
bool Service::ReadyFor(const modern_field_metadata::FieldMetadata& metadata,
    const source_handle_roots::Service& roots)const noexcept{
 return installed.owner==this&&installed.installed&&installed.metadata==&metadata&&installed.roots==&roots&&
     metadata.Calibrated()&&roots.Calibrated()&&ready.load(std::memory_order_acquire);
}
}

#include "platform/common/native_initializer_episode.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android11/class_initialization.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <type_traits>
#include <cstring>
namespace artdcevm::native_initializer_episode {
namespace {
using namespace abi::arm64::android11;
enum Fault : uint32_t {kLinkerMismatch=1u,kInitializationFlags=2u,
 kInitializationCompletion=4u,kWaitEntry=8u,kWaitCompletion=16u};
struct Trial {
 jobject fixture=nullptr;
 bool failure=false,lock_source_verified=false;
 std::atomic<void*> owner{nullptr},waiter{nullptr},linker{nullptr};
 std::atomic<uint32_t> tid{0},init_enter{0},init_exit{0},terminal_enter{0},terminal_exit{0},reentrant_enter{0},reentrant_exit{0},reentrant_tid{0},wait_enter{0},wait_exit{0},faults{0};
 std::atomic<bool> body_ready{false};
 std::atomic<uint32_t> lock_constructed{0},lock_destroyed{0},inner_entry_faults{0};
 std::atomic<uint32_t> lock_entry_faults{0},body_faults{0};
 std::atomic<void*> reentrant_owner{nullptr};
};
struct Installed {
 const Service* owner=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 const source_handle_roots::Service* roots=nullptr;
 JavaVM* vm=nullptr;jobject anchor=nullptr;void*class_linker=nullptr;
 void*(*current)()=nullptr;
 InitializeClass initialize=nullptr;WaitForInitializeClass wait=nullptr;
 WaitIgnoringInterrupts inner_wait=nullptr;
 ClassLockConstruct lock_construct=nullptr;ClassLockDestroy lock_destroy=nullptr;
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
struct SourceEntry {
 SourceEntry*previous;const Service*owner;void*linker;void*self;
 const void*slot;uint64_t epoch;bool statics,parents;
 uint32_t inner_entries=0,inner_returns=0;bool waiting=false;
 void*lock=nullptr;const void*lock_slot=nullptr;bool lock_destroyed=false;
};
thread_local SourceEntry*source_entry=nullptr;
thread_local uint64_t source_epoch=0;
struct EntryLifetime {
 SourceEntry entry;
 EntryLifetime(void*linker,void*self,ClassHandle handle,bool statics,bool parents):
     entry{source_entry,installed.owner,linker,self,handle.slot,++source_epoch,statics,parents}{
  if(!entry.epoch)std::abort();source_entry=&entry;
 }
 ~EntryLifetime(){if(source_entry!=&entry)std::abort();source_entry=entry.previous;}
};
struct BodyRequest{Trial* trial;JNIEnv* env;void* self;uint32_t tid;};
bool StateAt(const void*,uint32_t*,uint32_t*);
}

bool CaptureBody(const runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<BodyRequest*>(raw);void* klass=nullptr;uint32_t status=0,tid=0;
 return context.env==request.env&&context.self==request.self&&context.vm==installed.vm&&
     installed.roots->BorrowOwnedStrongGlobal(context,request.trial->fixture,&klass)&&
     StateAt(klass,&status,&tid)&&status==13&&tid==request.tid;
}
struct LockCalibration {Trial* trial;bool verified=false;};
bool CalibrateClassLock(const runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<LockCalibration*>(raw);void* klass=nullptr;
 if(context.self!=installed.current()||context.vm!=installed.vm||
    !installed.roots->BorrowOwnedStrongGlobal(context,request.trial->fixture,&klass))return false;
 auto scope=installed.roots->CreateScope(1,nullptr);if(!scope)return false;
 scope->Set(0,klass);ClassHandle handle{scope->Slot(0)};SourceClassLock lock{};
 installed.lock_construct(&lock,context.self,handle);
 void* actual=nullptr,*expected=nullptr;
 const bool valid=lock.self==context.self&&lock.object.slot==handle.slot&&
    installed.roots->BorrowHandle(context,lock.object.slot,&actual)&&actual&&
    installed.roots->BorrowOwnedStrongGlobal(context,request.trial->fixture,&expected)&&actual==expected;
 installed.lock_destroy(&lock);
 request.verified=valid;return valid;
}
static_assert(std::is_same<decltype(&CalibrateClassLock),runnable_jni::Operation>::value,"Actual Class-lock calibration callback ABI");
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
void CompleteInnerWait(SourceEntry& entry,Trial& trial,void* self,ClassHandle handle,bool result){
 if(!installed.inner_wait||!entry.waiting)return;
 uint32_t status=0,tid=0;
 if(!entry.inner_entries||entry.inner_entries!=entry.inner_returns||
    entry.self!=self||entry.linker!=trial.linker.load(std::memory_order_acquire)||
    self!=trial.waiter.load(std::memory_order_acquire)||
    !ReadHandle(trial,self,handle,&status,&tid)||result==trial.failure||
    (trial.failure?status!=2:(status<14||status>15)))
  trial.faults.fetch_or(kWaitCompletion,std::memory_order_release);
 // One completed outer InitializeClass, not one monitor wake, is the source
 // wait outcome. Spurious wakeups may have returned while status stayed 13.
 trial.wait_exit.fetch_add(1,std::memory_order_release);
}
bool HookInitialize(void* linker,void* self,ClassHandle handle,bool statics,bool parents){
 EntryLifetime episode(linker,self,handle,statics,parents);
 BorrowTrial borrowed;Trial* trial=borrowed.value;uint32_t status=0,tid=0;
 const bool matched=trial&&ReadHandle(*trial,self,handle,&status,&tid);
 // Original source may revisit a poisoned or already initialized Class. The
 // Java error oracle intentionally does so; these terminal fast returns are
 // distinct from the one owner and one waiting initialization invocation.
 const bool terminal=matched&&(status==2||(status>=14&&status<=15));
 // AOSP InitializeClass permits the initializing thread to reenter this same
 // Class and return true while it remains initializing. Prove an actual outer
 // invocation with a freshly read identical owned root; a foreign waiter or an
 // unrelated recursive initialization is never classified by status alone.
 bool reentrant=false;
 if(matched&&status==13&&tid)for(auto* outer=episode.entry.previous;outer;outer=outer->previous){
  uint32_t outer_status=0,outer_tid=0;
  if(outer->owner==installed.owner&&outer->self==self&&outer->linker==linker&&
     ReadHandle(*trial,self,ClassHandle{outer->slot},&outer_status,&outer_tid)&&
     outer_status==13&&outer_tid==tid){reentrant=true;break;}
 }
 if(reentrant){
  void* empty=nullptr;trial->reentrant_owner.compare_exchange_strong(empty,self);
  uint32_t zero=0;trial->reentrant_tid.compare_exchange_strong(zero,tid);
  if(trial->reentrant_owner.load()!=self||trial->reentrant_tid.load()!=tid)
   trial->faults.fetch_or(kInitializationCompletion,std::memory_order_release);
 }
 if(!matched)borrowed.Release();
 if(matched){void*empty=nullptr;trial->linker.compare_exchange_strong(empty,linker);
  if(!linker||trial->linker.load()!=linker)trial->faults.fetch_or(kLinkerMismatch,std::memory_order_release);
  (terminal?trial->terminal_enter:(reentrant?trial->reentrant_enter:trial->init_enter)).fetch_add(1,std::memory_order_relaxed);
  if(!statics||!parents)trial->faults.fetch_or(kInitializationFlags,std::memory_order_release);}
 const bool result=installed.initialize(linker,self,handle,statics,parents);
 if(matched)CompleteInnerWait(episode.entry,*trial,self,handle,result);
 if(matched){uint32_t completed=0,unused=0;
  const bool completion=ReadHandle(*trial,self,handle,&completed,&unused)&&
      (reentrant?result&&completed==13&&unused==tid:
       terminal?(status==2?trial->failure&&!result&&completed==2:
                           !trial->failure&&result&&completed>=status&&completed<=15):
                result!=trial->failure&&(trial->failure?completed==2:(completed>=14&&completed<=15)));
  if(!completion)trial->faults.fetch_or(kInitializationCompletion,std::memory_order_release);
  (terminal?trial->terminal_exit:(reentrant?trial->reentrant_exit:trial->init_exit)).fetch_add(1,std::memory_order_release);
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
   trial->faults.fetch_or(kWaitEntry,std::memory_order_release);
 }
 // Original source owns ObjectLock::WaitIgnoringInterrupts, status rereads,
 // NCDFE synthesis and the eventual return. The observer never simulates it.
 const bool result=installed.wait(linker,handle,self,lock);
 if(matched){uint32_t completed=0,unused=0;
  if(!ReadHandle(*trial,self,handle,&completed,&unused)||
     result==trial->failure||(trial->failure?completed!=2:(completed<14||completed>15)))
   trial->faults.fetch_or(kWaitCompletion,std::memory_order_release);
  trial->wait_exit.fetch_add(1,std::memory_order_release);
 }
 return result;
}
// Original public constructor arguments establish the opaque lock receiver's
// actual same-Thread/live-Class Handle. Its members are never interpreted.
void HookClassLockConstruct(void* lock,void* self,ClassHandle handle){
 BorrowTrial borrowed;Trial* trial=borrowed.value;SourceEntry* entry=source_entry;
 uint32_t status=0,tid=0;
 const bool matched=trial&&entry&&entry->owner==installed.owner&&entry->self==self&&
     ReadHandle(*trial,self,handle,&status,&tid)&&ReadHandle(*trial,self,{entry->slot},&status,&tid);
 if(!matched)borrowed.Release();
 installed.lock_construct(lock,self,handle);
 if(matched){
  // Scalar diagnostics preserve the original short-circuit: reborrow only if
  // the entry, receiver and tracked-lock checks all succeeded.
  uint32_t predicates=0;
  if(source_entry!=entry)predicates=1u;
  else if(!lock)predicates=2u;
  else if(entry->lock)predicates=4u;
  else if(!ReadHandle(*trial,self,handle,&status,&tid))predicates=8u;
  if(predicates){trial->lock_entry_faults.fetch_or(predicates,std::memory_order_release);
   trial->faults.fetch_or(kWaitEntry,std::memory_order_release);}
  else{entry->lock=lock;entry->lock_slot=handle.slot;entry->lock_destroyed=false;trial->lock_constructed.fetch_add(1,std::memory_order_relaxed);}
 }
}
void HookClassLockDestroy(void* lock){
 auto* entry=source_entry;BorrowTrial borrowed;auto* trial=borrowed.value;
 bool matched=entry&&entry->lock==lock;
 if(!matched&&entry&&trial&&trial->lock_source_verified&&lock){
  SourceClassLock source_lock{};size_t readable=0;uint32_t status=0,tid=0;
  if(modern_field_metadata::ReadMappingExtent(nullptr,lock,&readable)&&readable>=sizeof(source_lock)){
   std::memcpy(&source_lock,lock,sizeof(source_lock));
   matched=source_lock.self==entry->self&&ReadHandle(*trial,entry->self,source_lock.object,&status,&tid);
  }
 }
 if(!matched)borrowed.Release();
 installed.lock_destroy(lock);
 if(matched&&entry&&source_entry==entry){
  if(trial)trial->lock_destroyed.fetch_add(1,std::memory_order_relaxed);
  entry->lock=nullptr;entry->lock_slot=nullptr;entry->lock_destroyed=true;
 }
}
// The fixed WaitForInitializeClass source calls this exact member inside its
// status-rereading loop. The active outer source entry provides the live Class
// Handle and actual Thread; no ObjectLock member or monitor layout is read.
void HookInnerWait(void* lock){
 BorrowTrial borrowed;Trial* trial=borrowed.value;SourceEntry* entry=source_entry;
 uint32_t status=0,tid=0;
 const bool matched=trial&&entry&&entry->owner==installed.owner&&
     ReadHandle(*trial,entry->self,{entry->slot},&status,&tid);
 if(!matched)borrowed.Release();
 if(matched){
  uint32_t lock_status=0,lock_tid=0;
  SourceClassLock source_lock{};size_t readable=0;
  const bool source_readable=trial->lock_source_verified&&lock&&
      modern_field_metadata::ReadMappingExtent(nullptr,lock,&readable)&&readable>=sizeof(source_lock);
  if(source_readable)std::memcpy(&source_lock,lock,sizeof(source_lock));
  // Read the fixed declaration only after actual original construction proved
  // it. The caller's own constructor may have been inlined by the source compiler.
  const void* lock_slot=source_readable&&!entry->lock_destroyed&&source_lock.self==entry->self?source_lock.object.slot:nullptr;
  // Record independent predicates without changing the required conjunction.
  // A named constructor can exist even when this original caller inlined it.
  // An uncalibrated lock remains refused rather than accepted by TLS alone.
  uint32_t faults=0;
  if(!lock)faults|=1u;
  if(entry->lock&&entry->lock!=lock)faults|=2u;
  if(!lock_slot)faults|=4u;
  if(!lock_slot||!ReadHandle(*trial,entry->self,{lock_slot},&lock_status,&lock_tid))faults|=8u;
  if(lock_status!=status||lock_tid!=tid)faults|=16u;
  if(entry->self!=installed.current())faults|=32u;
  if(entry->self!=trial->waiter.load(std::memory_order_acquire)||entry->self==trial->owner.load(std::memory_order_acquire))faults|=64u;
  if(!trial->body_ready.load(std::memory_order_acquire))faults|=128u;
  if(status!=13||tid!=trial->tid.load(std::memory_order_acquire))faults|=256u;
  if(entry->linker!=trial->linker.load(std::memory_order_acquire))faults|=512u;
  if(faults){trial->inner_entry_faults.fetch_or(faults,std::memory_order_relaxed);
   trial->faults.fetch_or(kWaitEntry,std::memory_order_release);}
  ++entry->inner_entries;
  if(!entry->waiting){entry->waiting=true;
   if(trial->wait_enter.fetch_add(1,std::memory_order_acq_rel)!=0)
    trial->faults.fetch_or(kWaitEntry,std::memory_order_release);
  }
 }
 installed.inner_wait(lock);
 if(matched){uint32_t after=0,unused=0;
  if(source_entry!=entry||!ReadHandle(*trial,entry->self,{entry->slot},&after,&unused)||
     (after!=13&&after!=2&&(after<14||after>15)))
   trial->faults.fetch_or(kWaitCompletion,std::memory_order_release);
  ++entry->inner_returns;
 }
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
 const uint32_t body_faults=trial->faults.load(std::memory_order_acquire);
 trial->body_faults.store(body_faults,std::memory_order_release);
 return body_faults==0?JNI_TRUE:JNI_FALSE;
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

namespace {
struct CaptureRequest {const Service*owner;const void*entry;uint64_t epoch;bool verified=false;};
bool CaptureSourceRoot(const runnable_jni::Context&context,void*raw){
 auto&r=*static_cast<CaptureRequest*>(raw);void*managed=nullptr;
 if(!source_entry||source_entry!=r.entry||source_entry->epoch!=r.epoch||
    source_entry->owner!=r.owner||source_entry->self!=context.self)return false;
 r.verified=installed.roots->BorrowHandle(context,source_entry->slot,&managed)&&managed;
 return r.verified;
}
}
bool Service::UsesClassLinker(const void*linker)const noexcept{
 return linker&&installed.class_linker==linker&&installed.metadata&&installed.roots&&
     ReadyFor(*installed.metadata,*installed.roots);
}
bool Service::CaptureCurrent(JNIEnv*env,Witness*out)const noexcept{
 if(!out)return false;*out={};
 if(!env||!installed.metadata||!installed.roots||!ReadyFor(*installed.metadata,*installed.roots)||
    !source_entry||source_entry->owner!=this||!source_entry->self||
    !installed.current||installed.current()!=source_entry->self)return false;
 CaptureRequest request{this,source_entry,source_entry->epoch};
 try{
  if(runnable_executor::With(env,installed.anchor,CaptureSourceRoot,&request)!=runnable_jni::Result::kSuccess||
     !request.verified||source_entry!=request.entry||source_entry->epoch!=request.epoch)return false;
 }catch(...){return false;}
 out->owner_=this;out->entry_=source_entry;out->self_=source_entry->self;out->epoch_=source_entry->epoch;return true;
}
bool Service::MatchesSourceClass(const Witness&w,const modern_field_metadata::ReadContext&context,
    const void*klass)const noexcept{
 if(!klass||!context.owner||!context.extent||w.owner_!=this||!source_entry||
    w.entry_!=source_entry||w.epoch_!=source_entry->epoch||w.self_!=source_entry->self||
    !installed.current||installed.current()!=w.self_||!installed.metadata||!installed.roots||
    !ReadyFor(*installed.metadata,*installed.roots))return false;
 for(const SourceEntry*entry=source_entry;entry;entry=entry->previous){
  if(entry->owner!=this||entry->self!=w.self_||!entry->linker||entry->linker!=installed.class_linker||!entry->slot||
     !entry->epoch||!entry->statics||!entry->parents)return false;
  size_t extent=0;uint32_t value=0;
  if(!modern_field_metadata::ReadMappingExtent(&context,entry->slot,&extent)||extent<4)return false;
  std::memcpy(&value,entry->slot,4);
  if(uintptr_t(value)==uintptr_t(klass))return true;
 }
 return false;
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
 void* inner=wait?nullptr:hooks.FindSymbol("libart.so",kWaitIgnoringInterrupts);
 void* construct=inner?hooks.FindSymbol("libart.so",kClassLockConstruct):nullptr;
 void* destroy=inner?hooks.FindSymbol("libart.so",kClassLockDestroy):nullptr;
 using namespace abi::common;
 auto suspend=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto resume=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!candidate.current||!initialize||(!wait&&!inner)||initialize==wait||initialize==inner||(inner&&(!construct||!destroy||construct==destroy||construct==inner||destroy==inner))||!suspend||!resume||
    env->GetJavaVM(&candidate.vm)!=JNI_OK||!candidate.vm||
    !roots.OrdinaryNative({candidate.current(),env,candidate.vm}))
  return Fail(error,"Original initialization/wait declarations or ordinary Native owner unavailable");
 candidate.anchor=env->NewGlobalRef(anchor);if(!candidate.anchor||env->ExceptionCheck())return false;
 void* tokens[4]{};bool ok=false;
 {
  SuspendAll stop{};suspend(&stop,"artdcevm source initialization observers",false);
  installed=candidate;
  tokens[0]=hooks.Install(initialize,reinterpret_cast<void*>(HookInitialize),reinterpret_cast<void**>(&installed.initialize),error);
  if(tokens[0])tokens[1]=wait?hooks.Install(wait,reinterpret_cast<void*>(HookWait),reinterpret_cast<void**>(&installed.wait),error):
      hooks.Install(inner,reinterpret_cast<void*>(HookInnerWait),reinterpret_cast<void**>(&installed.inner_wait),error);
  if(inner&&tokens[1])tokens[2]=hooks.Install(construct,reinterpret_cast<void*>(HookClassLockConstruct),reinterpret_cast<void**>(&installed.lock_construct),error);
  if(inner&&tokens[2])tokens[3]=hooks.Install(destroy,reinterpret_cast<void*>(HookClassLockDestroy),reinterpret_cast<void**>(&installed.lock_destroy),error);
  ok=tokens[0]&&tokens[1]&&installed.initialize&&(wait?installed.wait!=nullptr:
      tokens[2]&&tokens[3]&&installed.inner_wait&&installed.lock_construct&&installed.lock_destroy);
  if(!ok){bool removed=true;for(int i=3;i>=0;--i)if(tokens[i]){std::string rollback;if(!hooks.Remove(tokens[i],&rollback))removed=false;}
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
  Trial trial;trial.fixture=strong;trial.failure=failure;
  if(installed.inner_wait){LockCalibration calibration{&trial};
   trial.lock_source_verified=runnable_executor::With(env,installed.anchor,CalibrateClassLock,&calibration)==runnable_jni::Result::kSuccess&&calibration.verified&&!env->ExceptionCheck();
   if(!trial.lock_source_verified){env->DeleteGlobalRef(strong);env->DeleteLocalRef(local);ok=false;Fail(error,"Original Class-lock constructor source declaration calibration rejected");break;}}
  active.store(&trial,std::memory_order_seq_cst);
  const jint result=env->CallStaticIntMethod(oracle,run,jboolean(failure));
  active.store(nullptr,std::memory_order_seq_cst);
  // Also drain an unrelated source hook which may have loaded this epoch before
  // it was unpublished. No Class/global/stack Trial is freed while it is read.
  while(readers.load(std::memory_order_seq_cst)!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ok=!env->ExceptionCheck()&&result==1&&trial.faults.load(std::memory_order_acquire)==0&&
      trial.owner.load()!=nullptr&&trial.waiter.load()!=nullptr&&trial.owner.load()!=trial.waiter.load()&&
      trial.init_enter.load()==2&&trial.init_exit.load()==2&&
      trial.reentrant_enter.load()==trial.reentrant_exit.load()&&
      (!trial.reentrant_enter.load()||(trial.reentrant_owner.load()==trial.owner.load()&&trial.reentrant_tid.load()==trial.tid.load()))&&trial.terminal_enter.load()==trial.terminal_exit.load()&&trial.wait_enter.load()==1&&trial.wait_exit.load()==1&&
      trial.linker.load()!=nullptr&&(!installed.class_linker||installed.class_linker==trial.linker.load());
  if(ok)installed.class_linker=trial.linker.load();
  else if(error)*error="Original initialization episode "+std::string(failure?"error":"success")+
      " rejected [javaResult="+std::to_string(result)+" faults="+std::to_string(trial.faults.load())+
      " bodyReady="+std::to_string(trial.body_ready.load())+" owner="+std::to_string(trial.owner.load()!=nullptr)+
      " waiter="+std::to_string(trial.waiter.load()!=nullptr)+" init="+std::to_string(trial.init_enter.load())+
      "/"+std::to_string(trial.init_exit.load())+" terminal="+std::to_string(trial.terminal_enter.load())+
      "/"+std::to_string(trial.terminal_exit.load())+" reentrant="+std::to_string(trial.reentrant_enter.load())+
      "/"+std::to_string(trial.reentrant_exit.load())+" wait="+std::to_string(trial.wait_enter.load())+
      "/"+std::to_string(trial.wait_exit.load())+" lock="+std::to_string(trial.lock_constructed.load())+
      "/"+std::to_string(trial.lock_destroyed.load())+" innerEntryFaults="+std::to_string(trial.inner_entry_faults.load())+
      " lockEntryFaults="+std::to_string(trial.lock_entry_faults.load())+
      " bodyFaults="+std::to_string(trial.body_faults.load())+"]";
  env->DeleteGlobalRef(strong);env->DeleteLocalRef(local);
 }
 env->DeleteLocalRef(oracle);
 if(!ok){if(error&&error->empty())*error="Original initialization/wait roots or Java body oracle unavailable";return false;}
 ready.store(true,std::memory_order_release);if(error)error->clear();return true;
}
bool Service::ReadyFor(const modern_field_metadata::FieldMetadata& metadata,
    const source_handle_roots::Service& roots)const noexcept{
 return installed.owner==this&&installed.installed&&installed.metadata==&metadata&&installed.roots==&roots&&
     metadata.Calibrated()&&roots.Calibrated()&&ready.load(std::memory_order_acquire);
}
}

#include "platform/common/native_initializer_episode.h"
#include "platform/abi/arm64/android11/class_initialization.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
JavaVM* EpisodeVM();
artdcevm::abi::arm64::android11::ReleaseObjectPointer EpisodeGlobal(jobject);
void RunEpisodes(artdcevm::modern_field_metadata::FieldMetadata&,artdcevm::source_handle_roots::Service&);
#define main RootsFixtureMain
#include "episode_root_fixture.inc"
#undef main
namespace artdcevm::runnable_executor {bool Available(){return true;}}
namespace {
using namespace abi::arm64::android11;
using BodyNative=jboolean(*)(JNIEnv*,jclass,jclass,jint);
using WaiterNative=jboolean(*)(JNIEnv*,jclass);
BodyNative body_native=nullptr;WaiterNative waiter_native=nullptr;
abi::arm64::android11::InitializeClass initialize_proxy=nullptr;WaitForInitializeClass wait_proxy=nullptr;WaitIgnoringInterrupts inner_proxy=nullptr;ClassLockConstruct construct_proxy=nullptr;ClassLockDestroy destroy_proxy=nullptr;
thread_local JNIEnv* episode_env=&env;

std::mutex original_monitor;std::condition_variable original_condition;
std::atomic<int> original_waits{0},original_bodies{0};
std::atomic<bool> source_moved{false};
std::atomic<const void*> owner_slot{nullptr},waiter_slot{nullptr};
enum class Case{Positive,MovingRoots,MissingWait,WrongWaitReturn,WrongInitReturn,WrongCompletion,WrongUnresolvedError,WrongPublicResult,WrongBodyTid,UnownedWaitHandle,WrongWaitSelf,WrongClassLinker,RepeatedSuccess,WrongTerminalReturn,WrongTerminalCompletion,ReentrantOwner,WrongReentrantReturn,WrongReentrantCompletion,WrongReentrantTid,WrongReentrantEntryTid,InnerPositive,InnerInlinedCtor,InnerMovingRoots,InnerSpurious,InnerMultiple,InnerWrongStatus,InnerWrongSelf,InnerWrongOwner,InnerDestroyedLock,InnerInlinedDestroyedLock,InnerMissingCtor,InnerBadConstructor,InnerInlinedDestroyThenConstruct,InnerEarlyReturn,EnteringPending};
Case selected=Case::Positive;
bool InnerCase(){return selected>=Case::InnerPositive&&selected<=Case::InnerEarlyReturn;}
bool PositiveCase(){return selected==Case::Positive||selected==Case::MovingRoots||selected==Case::RepeatedSuccess||selected==Case::ReentrantOwner||selected==Case::InnerPositive||selected==Case::InnerInlinedCtor||selected==Case::InnerMovingRoots||selected==Case::InnerSpurious||selected==Case::InnerMultiple;}
thread_local bool episode_pending=false;
jboolean EpisodeException(JNIEnv*){return episode_pending?JNI_TRUE:JNI_FALSE;}
thread_local class_metadata::Class17* inner_class=nullptr;
thread_local unsigned inner_cycles=0;
int type_id,run_episode_id;
constexpr uint32_t source_tid=913;
uint32_t LoadStatus(class_metadata::Class17* type){return __atomic_load_n(&type->status,__ATOMIC_SEQ_CST)>>28;}
void StoreStatus(class_metadata::Class17* type,uint32_t status){__atomic_store_n(&type->status,status<<28,__ATOMIC_SEQ_CST);}
// Separate original source declarations/algorithms: the observer never supplies
// these locks, status transitions, wait outcomes or condition notification.
struct OriginalClassHandle{const uint32_t* slot;};
struct OriginalSourceClassLock {void* self;OriginalClassHandle object;};
thread_local std::map<void*,std::unique_ptr<std::unique_lock<std::mutex>>> original_locks;
std::unique_lock<std::mutex>& OriginalLock(void* opaque){return *original_locks.at(opaque);}

void OriginalClassLockConstruct(void* opaque,void* self,ClassHandle handle){
 assert(self==current&&handle.slot);assert(original_locks.find(opaque)==original_locks.end());
 auto& source=*static_cast<OriginalSourceClassLock*>(opaque);source.self=selected==Case::InnerBadConstructor?foreign_thread.data():self;source.object.slot=static_cast<const uint32_t*>(handle.slot);
 original_locks.emplace(opaque,std::make_unique<std::unique_lock<std::mutex>>(original_monitor));
}
void OriginalClassLockDestroy(void* opaque){
 auto& lock=OriginalLock(opaque);assert(lock.owns_lock());lock.unlock();original_locks.erase(opaque);
}
void OriginalInnerWait(void* opaque){
 auto& lock=OriginalLock(opaque);assert(lock.owns_lock());
 assert(inner_class);++inner_cycles;
 if(selected==Case::InnerWrongStatus)StoreStatus(inner_class,13);
 if((selected==Case::InnerSpurious&&inner_cycles==1)||(selected==Case::InnerMultiple&&inner_cycles<=2))return;
 Put32(current,0,92u<<24);original_condition.wait(lock);Put32(current,0,0);
}
bool OriginalWait(void*,ClassHandle abi_handle,void* self,void* opaque){
 const OriginalClassHandle handle{static_cast<const uint32_t*>(abi_handle.slot)};
 assert(self==(selected==Case::WrongWaitSelf?foreign_thread.data():current)&&Read32(current)==0&&opaque);
 auto& lock=InnerCase()?OriginalLock(opaque):*static_cast<std::unique_lock<std::mutex>*>(opaque);assert(lock.owns_lock());
 auto* type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(*handle.slot));
 ++original_waits;inner_cycles=0;
 if(selected==Case::InnerEarlyReturn)return true;
 for(;;){
  type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
  if(LoadStatus(type)!=13)break;
  if(InnerCase()){
   inner_class=type;
   if((selected==Case::InnerDestroyedLock||selected==Case::InnerInlinedDestroyedLock)){destroy_proxy(opaque);OriginalClassLockConstruct(opaque,self,abi_handle);}
   if(selected==Case::InnerWrongStatus)StoreStatus(type,12);
   void* previous=current;if(selected==Case::InnerWrongSelf)current=foreign_thread.data();
   inner_proxy(opaque);current=previous;
  }else{Put32(current,0,92u<<24);original_condition.wait(lock);Put32(current,0,0);}
 }
 type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
 bool result=LoadStatus(type)>=14;return selected==Case::WrongWaitReturn?!result:result;
}
thread_local bool original_reentrant=false;
bool OriginalInitialize(void* linker,void* self,ClassHandle abi_handle,bool statics,bool parents){
 const OriginalClassHandle handle{static_cast<const uint32_t*>(abi_handle.slot)};
 assert(self==current&&Read32(current)==0&&statics&&parents);
 auto* type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(*handle.slot));
 std::unique_lock<std::mutex> legacy_lock(original_monitor,std::defer_lock);OriginalSourceClassLock native_lock{};
 uint32_t foreign_class=uint32_t(uintptr_t(type==new_class?old_class:new_class));
 if(InnerCase())((selected==Case::InnerInlinedCtor||selected==Case::InnerInlinedDestroyedLock)?OriginalClassLockConstruct:construct_proxy)(&native_lock,self,selected==Case::InnerWrongOwner&&LoadStatus(type)==13?ClassHandle{&foreign_class}:abi_handle);
 else legacy_lock.lock();
 // Independent source sequence: an inlined original destructor releases the
 // first real monitor, then a named constructor creates the next lock scope.
 // No observer supplies either operation or any synthetic root/status.
 if(selected==Case::InnerInlinedDestroyThenConstruct){
  OriginalClassLockDestroy(&native_lock);
  construct_proxy(&native_lock,self,abi_handle);
 }
 auto& lock=InnerCase()?OriginalLock(&native_lock):legacy_lock;
 struct Destruction {void*receiver;bool inner;~Destruction(){if(inner)destroy_proxy(receiver);}} destruction{&native_lock,InnerCase()};
 if(LoadStatus(type)==2||LoadStatus(type)==3||LoadStatus(type)>=14){
  const bool result=LoadStatus(type)>=14;
  if(selected==Case::WrongTerminalCompletion)StoreStatus(type,12);
  return selected==Case::WrongTerminalReturn?!result:result;
 }
 if(LoadStatus(type)==13){
  if(original_reentrant){
   if(selected==Case::WrongReentrantCompletion)StoreStatus(type,14);
   if(selected==Case::WrongReentrantTid)++type->clinit_thread_id_or_hash;
   return selected!=Case::WrongReentrantReturn;
  }
  uint32_t foreign=uint32_t(uintptr_t(type));ClassHandle waiter_handle{selected==Case::UnownedWaitHandle?&foreign:handle.slot};
  bool result=(InnerCase()?OriginalWait:wait_proxy)(linker,waiter_handle,selected==Case::WrongWaitSelf?foreign_thread.data():self,InnerCase()?static_cast<void*>(&native_lock):static_cast<void*>(&lock));
  return selected==Case::WrongInitReturn?!result:result;
 }
 assert(LoadStatus(type)==12);__atomic_store_n(&type->clinit_thread_id_or_hash,source_tid,__ATOMIC_SEQ_CST);StoreStatus(type,13);
 lock.unlock();Put32(current,0,92u<<24);
 ++original_bodies;
 bool body_ok=body_native(episode_env,reinterpret_cast<jclass>(parent_class),reinterpret_cast<jclass>(type),selected==Case::WrongBodyTid?source_tid+1:source_tid)==JNI_TRUE;
 Put32(current,0,0);
 if(selected==Case::ReentrantOwner||selected==Case::WrongReentrantReturn||selected==Case::WrongReentrantCompletion||selected==Case::WrongReentrantTid||selected==Case::WrongReentrantEntryTid){
  if(selected==Case::WrongReentrantEntryTid)++type->clinit_thread_id_or_hash;
  original_reentrant=true;
  (void)initialize_proxy(linker,self,abi_handle,statics,parents);
  original_reentrant=false;
  if(selected==Case::WrongReentrantEntryTid)--type->clinit_thread_id_or_hash;
 }
 lock.lock();
 bool success=body_ok&&type==new_class;
 if((selected==Case::MovingRoots||selected==Case::InnerMovingRoots)&&success){
  assert(owner_slot.load()&&waiter_slot.load());
  std::memcpy(moved_class,type,160);
  // Independent collector updates the real source compressed cells and the
  // modeled strong-global table. No native Class address survives this step.
  __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(owner_slot.load())),uint32_t(uintptr_t(moved_class)),__ATOMIC_SEQ_CST);
  __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(waiter_slot.load())),uint32_t(uintptr_t(moved_class)),__ATOMIC_SEQ_CST);
  source_moved.store(true,std::memory_order_release);
  type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
 }
 StoreStatus(type,selected==Case::WrongCompletion?12u:(success?14u:(selected==Case::WrongUnresolvedError?3u:2u)));original_condition.notify_all();
 return selected==Case::WrongInitReturn?!success:success;
}
class EpisodeHooks final:public Hooks {
 public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,kInitializeClass))return reinterpret_cast<void*>(OriginalInitialize);
  if(!std::strcmp(name,kWaitForInitializeClass))return selected==Case::MissingWait||InnerCase()?nullptr:reinterpret_cast<void*>(OriginalWait);
  if(!std::strcmp(name,kWaitIgnoringInterrupts))return InnerCase()?reinterpret_cast<void*>(OriginalInnerWait):nullptr;
  if(!std::strcmp(name,kClassLockConstruct))return InnerCase()&&selected!=Case::InnerMissingCtor?reinterpret_cast<void*>(OriginalClassLockConstruct):nullptr;
  if(!std::strcmp(name,kClassLockDestroy))return InnerCase()?reinterpret_cast<void*>(OriginalClassLockDestroy):nullptr;
  return Hooks::FindSymbol(lib,name);
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  assert(stopped==1&&Read32(current)==(92u<<24));*original=target;
  if(target==reinterpret_cast<void*>(OriginalInitialize))initialize_proxy=reinterpret_cast<abi::arm64::android11::InitializeClass>(proxy);
  else if(target==reinterpret_cast<void*>(OriginalClassLockConstruct))construct_proxy=reinterpret_cast<ClassLockConstruct>(proxy);
  else if(target==reinterpret_cast<void*>(OriginalClassLockDestroy))destroy_proxy=reinterpret_cast<ClassLockDestroy>(proxy);
  else if(target==reinterpret_cast<void*>(OriginalInnerWait))inner_proxy=reinterpret_cast<WaitIgnoringInterrupts>(proxy);
  else {assert(target==reinterpret_cast<void*>(OriginalWait));wait_proxy=reinterpret_cast<WaitForInitializeClass>(proxy);}
  return target;
 }
 bool Remove(void*,std::string*)override{return true;}
};
jint VMEnv(JavaVM*,void** out,jint version){assert(version==JNI_VERSION_1_6);*out=episode_env;return JNI_OK;}
jint EpisodeGetVM(JNIEnv*,JavaVM** out){Jni();*out=EpisodeVM();return JNI_OK;}
jclass EpisodeFind(JNIEnv* e,const char* name){
 if(!std::strcmp(name,"com/github/ytlog/artdcevm/internal/InitializerEpisodeOracle")){Jni();return reinterpret_cast<jclass>(parent_class);}
 return Find(e,name);
}
jmethodID EpisodeMethod(JNIEnv* e,jclass type,const char* name,const char* signature){
 if(type==reinterpret_cast<jclass>(parent_class)){
  if(!std::strcmp(name,"type")){assert(!std::strcmp(signature,"(Z)Ljava/lang/Class;"));Jni();return reinterpret_cast<jmethodID>(&type_id);}
  if(!std::strcmp(name,"run")){assert(!std::strcmp(signature,"(Z)I"));Jni();return reinterpret_cast<jmethodID>(&run_episode_id);}
 }return StaticMethodId(e,type,name,signature);
}
jint EpisodeRegister(JNIEnv*,jclass type,const JNINativeMethod* methods,jint count){
 Jni();assert(type==reinterpret_cast<jclass>(parent_class)&&count==2);
 assert(!std::strcmp(methods[0].name,"body")&&!std::strcmp(methods[0].signature,"(Ljava/lang/Class;I)Z"));
 assert(!std::strcmp(methods[1].name,"waiter")&&!std::strcmp(methods[1].signature,"()Z"));
 body_native=reinterpret_cast<BodyNative>(methods[0].fnPtr);waiter_native=reinterpret_cast<WaiterNative>(methods[1].fnPtr);return JNI_OK;
}
jobject EpisodeType(JNIEnv*,jclass type,jmethodID method,va_list args){
 Jni();assert(type==reinterpret_cast<jclass>(parent_class)&&method==reinterpret_cast<jmethodID>(&type_id));
 return reinterpret_cast<jobject>(va_arg(args,int)?old_class:new_class);
}
bool OriginalThread(class_metadata::Class17* type,bool owner){
 void* previous_current=current;JNIEnv* previous_env=episode_env;
 alignas(4096) std::array<unsigned char,512> original_thread{};std::memcpy(original_thread.data(),thread.data(),512);
 JNIEnv local_env{&table};current=original_thread.data();episode_env=&local_env;
 std::memcpy(original_thread.data()+216,&current,8);std::memcpy(original_thread.data()+200,&episode_env,8);
 alignas(8) unsigned char scope[32]{};OriginalHeader header{nullptr,1};std::memcpy(scope,&header,sizeof(header));
 if(sized_scope)Put32(scope,12,1);Put32(scope,OriginalReferences(),uint32_t(uintptr_t(type)));Publish(scope);Put32(current,0,92u<<24);
 (owner?owner_slot:waiter_slot).store(scope+OriginalReferences(),std::memory_order_release);
 if(!owner&&waiter_native(episode_env,reinterpret_cast<jclass>(parent_class))!=JNI_TRUE){Publish(nullptr);current=previous_current;episode_env=previous_env;return false;}
 Put32(current,0,0);const bool result=initialize_proxy(selected==Case::WrongClassLinker&&!owner?static_cast<void*>(&runtime):static_cast<void*>(&linker),current,{scope+OriginalReferences()},true,true);
 assert(Top()==scope);Publish(nullptr);current=previous_current;episode_env=previous_env;return result;
}
jint EpisodeRun(JNIEnv* e,jclass type,jmethodID method,va_list args){
 if(method!=reinterpret_cast<jmethodID>(&run_episode_id))return StaticInt(e,type,method,args);
 Jni();bool fail=va_arg(args,int)!=0;auto* klass=fail?old_class:new_class;StoreStatus(klass,12);
 original_waits=0;original_bodies=0;bool owner_result=false,waiter_result=false;
 std::thread owner([&]{owner_result=OriginalThread(klass,true);});
 std::thread waiter([&]{waiter_result=OriginalThread(klass,false);});owner.join();waiter.join();
 const bool oracle=original_waits==1&&original_bodies==1&&owner_result==!fail&&waiter_result==!fail;
 // Match the actual Java oracle: a poisoned Class is accessed once more after
 // joining both threads. The original helper must refuse without another body
 // or source wait, while normal completed access can fast-return unchanged.
 const auto bodies_before=original_bodies.load(),waits_before=original_waits.load();
 bool terminal_ok=true;
 if(fail||selected==Case::RepeatedSuccess)terminal_ok=OriginalThread(klass,true)==!fail;
 if(selected==Case::WrongTerminalReturn||selected==Case::WrongTerminalCompletion)terminal_ok=true; // independent Java leaf does not hide observer mismatch.
 assert(original_waits==waits_before&&original_bodies==bodies_before);
 return oracle&&terminal_ok&&selected!=Case::WrongPublicResult?1:0;
}
}
abi::arm64::android11::ReleaseObjectPointer EpisodeGlobal(jobject value){
 return {source_moved.load(std::memory_order_acquire)&&value==reinterpret_cast<jobject>(new_class)?uintptr_t(moved_class):uintptr_t(value)};
}
JavaVM* EpisodeVM(){static JNIInvokeInterface_ methods=[] {JNIInvokeInterface_ value{};value.GetEnv=VMEnv;return value;}();static JavaVM value{&methods};return &value;}

native_initializer_episode::Service*witness_service=nullptr;
native_initializer_episode::Witness stale_witness;
jboolean WitnessBody(JNIEnv*e,jclass,jclass type,jint actual_tid){
 assert(witness_service&&actual_tid==source_tid);
 native_initializer_episode::Witness witness;
 assert(witness_service->CaptureCurrent(e,&witness));
 const void*slot=owner_slot.load();
 struct Bounds{const void*slot;} bounds{slot};
 modern_field_metadata::ReadContext context{&bounds,[](void*p,const void*address,size_t*out){
  auto&b=*static_cast<Bounds*>(p);if(address!=b.slot||!out)return false;*out=4;return true;
 }};
 const auto before=method_identity::MappingOsQueries();
 assert(witness_service->MatchesSourceClass(witness,context,type));
 assert(!witness_service->MatchesSourceClass(witness,context,old_class));
 native_initializer_episode::Service wrong_owner;
 assert(!wrong_owner.MatchesSourceClass(witness,context,type));
 auto denied=context;denied.extent=[](void*,const void*,size_t*){return false;};
 assert(!witness_service->MatchesSourceClass(witness,denied,type));
 native_initializer_episode::Witness empty;
 assert(!witness_service->MatchesSourceClass(empty,context,type));
 // Independent moving collector changes the actual compressed source root.
 // A witness keeps the source slot identity and never caches the Class value.
 std::memcpy(moved_class,type,160);
 __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(slot)),uint32_t(uintptr_t(moved_class)),__ATOMIC_SEQ_CST);
 assert(witness_service->MatchesSourceClass(witness,context,moved_class));
 assert(!witness_service->MatchesSourceClass(witness,context,type));
 __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(slot)),uint32_t(uintptr_t(type)),__ATOMIC_SEQ_CST);
 assert(witness_service->MatchesSourceClass(witness,context,type));
 assert(method_identity::MappingOsQueries()==before);stale_witness=witness;return JNI_TRUE;
}

void RunEpisodes(modern_field_metadata::FieldMetadata& metadata,source_handle_roots::Service& roots){
 assert(Read32(current)==(92u<<24));EpisodeHooks hooks;native_initializer_episode::Service observer;std::string error;
 auto saved=table;table.GetJavaVM=EpisodeGetVM;table.FindClass=EpisodeFind;table.GetStaticMethodID=EpisodeMethod;
 table.RegisterNatives=EpisodeRegister;table.CallStaticObjectMethodV=EpisodeType;table.CallStaticIntMethodV=EpisodeRun;
 if(selected==Case::EnteringPending){table.ExceptionCheck=EpisodeException;episode_pending=true;}
 const auto mutations_before=mutations,publications=retirement_publications;
 const bool ok=observer.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error);
 assert(ok==PositiveCase());
 if(selected==Case::EnteringPending){assert(episode_pending&&!initialize_proxy&&!inner_proxy&&!wait_proxy);episode_pending=false;}assert(observer.ReadyFor(metadata,roots)==ok);
 modern_field_metadata::FieldMetadata foreign=metadata;assert(!observer.ReadyFor(foreign,roots));
 assert(mutations==mutations_before&&retirement_publications==publications);
 if(selected==Case::WrongTerminalReturn||selected==Case::WrongTerminalCompletion){
  assert(!ok&&error.find("episode error")!=std::string::npos&&error.find("faults=4")!=std::string::npos&&error.find("terminal=1/1")!=std::string::npos);
 }
 if(selected==Case::WrongReentrantReturn||selected==Case::WrongReentrantCompletion||selected==Case::WrongReentrantTid){
  assert(!ok&&error.find("faults=4")!=std::string::npos&&error.find("reentrant=1/1")!=std::string::npos);
 }
 if(selected==Case::WrongPublicResult){
  assert(!ok&&error.find("lockEntryFaults=0")!=std::string::npos&&error.find("bodyFaults=0")!=std::string::npos);
 }
 if(selected==Case::InnerInlinedDestroyThenConstruct){
  assert(!ok&&error.find("lockEntryFaults=4")!=std::string::npos&&error.find("bodyFaults=8")!=std::string::npos);
 }
 if(selected==Case::WrongUnresolvedError)assert(!ok&&error.find("faults=20")!=std::string::npos);
 if(selected==Case::InnerDestroyedLock||selected==Case::InnerInlinedDestroyedLock){
  assert(!ok&&error.find("innerEntryFaults=28")!=std::string::npos);
 }
 if(selected==Case::InnerBadConstructor)assert(!ok&&error=="Original Class-lock constructor source declaration calibration rejected"&&original_bodies==0);
 if(selected==Case::InnerWrongOwner)assert(!ok&&error.find("innerEntryFaults=24")!=std::string::npos);
 if(selected==Case::InnerWrongStatus)assert(!ok&&error.find("innerEntryFaults=256")!=std::string::npos);
 if(selected==Case::WrongReentrantEntryTid)assert(!ok&&error.find("reentrant=1/1")!=std::string::npos);
 if(ok){native_initializer_episode::Service wrong_owner;assert(!wrong_owner.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error));assert(observer.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error));}
 if(ok&&selected==Case::Positive){
  native_initializer_episode::Witness absent;assert(!observer.CaptureCurrent(&env,&absent));
  assert(observer.UsesClassLinker(&linker)&&!observer.UsesClassLinker(&runtime)&&!observer.UsesClassLinker(nullptr));
  witness_service=&observer;body_native=WitnessBody;StoreStatus(new_class,12);
  std::thread witness_owner([]{assert(OriginalThread(new_class,true));});witness_owner.join();
  modern_field_metadata::ReadContext outside{&observer,[](void*,const void*,size_t*out){*out=4;return true;}};
  assert(!observer.MatchesSourceClass(stale_witness,outside,new_class));witness_service=nullptr;
 }
 table=saved;
}
int main(){
 for(Case test:{Case::Positive,Case::MovingRoots,Case::MissingWait,Case::WrongWaitReturn,Case::WrongInitReturn,Case::WrongCompletion,Case::WrongUnresolvedError,
Case::WrongPublicResult,Case::WrongBodyTid,Case::UnownedWaitHandle,Case::WrongWaitSelf,Case::WrongClassLinker,Case::RepeatedSuccess,Case::WrongTerminalReturn,Case::WrongTerminalCompletion,Case::ReentrantOwner,Case::WrongReentrantReturn,Case::WrongReentrantCompletion,Case::WrongReentrantTid,Case::WrongReentrantEntryTid,Case::InnerPositive,Case::InnerInlinedCtor,Case::InnerMovingRoots,Case::InnerSpurious,Case::InnerMultiple,Case::InnerWrongStatus,Case::InnerWrongSelf,Case::InnerWrongOwner,Case::InnerDestroyedLock,Case::InnerInlinedDestroyedLock,Case::InnerMissingCtor,Case::InnerBadConstructor,Case::InnerInlinedDestroyThenConstruct,Case::InnerEarlyReturn,Case::EnteringPending}){
  pid_t child=fork();assert(child>=0);if(child==0){selected=test;int result=RootsFixtureMain();_exit(result);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 std::cout<<"Actual production init/wait observers + real calibrated roots + independent condition-variable source waiter normal/error/owner/return/handle negative episodes PASS\n";
}

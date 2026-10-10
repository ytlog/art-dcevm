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
abi::arm64::android11::InitializeClass initialize_proxy=nullptr;WaitForInitializeClass wait_proxy=nullptr;
thread_local JNIEnv* episode_env=&env;

std::mutex original_monitor;std::condition_variable original_condition;
std::atomic<int> original_waits{0},original_bodies{0};
std::atomic<bool> source_moved{false};
std::atomic<const void*> owner_slot{nullptr},waiter_slot{nullptr};
enum class Case{Positive,MovingRoots,MissingWait,WrongWaitReturn,WrongInitReturn,WrongCompletion,WrongPublicResult,WrongBodyTid,UnownedWaitHandle,WrongWaitSelf};
Case selected=Case::Positive;
int type_id,run_episode_id;
constexpr uint32_t source_tid=913;
uint32_t LoadStatus(class_metadata::Class17* type){return __atomic_load_n(&type->status,__ATOMIC_SEQ_CST)>>28;}
void StoreStatus(class_metadata::Class17* type,uint32_t status){__atomic_store_n(&type->status,status<<28,__ATOMIC_SEQ_CST);}
// Separate original source declarations/algorithms: the observer never supplies
// these locks, status transitions, wait outcomes or condition notification.
struct OriginalClassHandle{const uint32_t* slot;};
bool OriginalWait(void*,ClassHandle abi_handle,void* self,void* opaque){
 const OriginalClassHandle handle{static_cast<const uint32_t*>(abi_handle.slot)};
 assert(self==(selected==Case::WrongWaitSelf?foreign_thread.data():current)&&Read32(current)==0&&opaque);
 auto& lock=*static_cast<std::unique_lock<std::mutex>*>(opaque);assert(lock.owns_lock());
 auto* type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(*handle.slot));
 ++original_waits;
 for(;;){
  type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
  if(LoadStatus(type)!=13)break;
  Put32(current,0,92u<<24);original_condition.wait(lock);Put32(current,0,0);
 }
 type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
 bool result=LoadStatus(type)>=14;return selected==Case::WrongWaitReturn?!result:result;
}
bool OriginalInitialize(void* linker,void* self,ClassHandle abi_handle,bool statics,bool parents){
 const OriginalClassHandle handle{static_cast<const uint32_t*>(abi_handle.slot)};
 assert(self==current&&Read32(current)==0&&statics&&parents);
 auto* type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(*handle.slot));
 std::unique_lock<std::mutex> lock(original_monitor);
 if(LoadStatus(type)==13){
  uint32_t foreign=uint32_t(uintptr_t(type));ClassHandle waiter_handle{selected==Case::UnownedWaitHandle?&foreign:handle.slot};
  bool result=wait_proxy(linker,waiter_handle,selected==Case::WrongWaitSelf?foreign_thread.data():self,&lock);
  return selected==Case::WrongInitReturn?!result:result;
 }
 assert(LoadStatus(type)==12);__atomic_store_n(&type->clinit_thread_id_or_hash,source_tid,__ATOMIC_SEQ_CST);StoreStatus(type,13);
 lock.unlock();Put32(current,0,92u<<24);
 ++original_bodies;
 bool body_ok=body_native(episode_env,reinterpret_cast<jclass>(parent_class),reinterpret_cast<jclass>(type),selected==Case::WrongBodyTid?source_tid+1:source_tid)==JNI_TRUE;
 Put32(current,0,0);lock.lock();
 bool success=body_ok&&type==new_class;
 if(selected==Case::MovingRoots&&success){
  assert(owner_slot.load()&&waiter_slot.load());
  std::memcpy(moved_class,type,160);
  // Independent collector updates the real source compressed cells and the
  // modeled strong-global table. No native Class address survives this step.
  __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(owner_slot.load())),uint32_t(uintptr_t(moved_class)),__ATOMIC_SEQ_CST);
  __atomic_store_n(static_cast<uint32_t*>(const_cast<void*>(waiter_slot.load())),uint32_t(uintptr_t(moved_class)),__ATOMIC_SEQ_CST);
  source_moved.store(true,std::memory_order_release);
  type=reinterpret_cast<class_metadata::Class17*>(uintptr_t(__atomic_load_n(handle.slot,__ATOMIC_SEQ_CST)));
 }
 StoreStatus(type,selected==Case::WrongCompletion?12u:(success?14u:3u));original_condition.notify_all();
 return selected==Case::WrongInitReturn?!success:success;
}
class EpisodeHooks final:public Hooks {
 public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,kInitializeClass))return reinterpret_cast<void*>(OriginalInitialize);
  if(!std::strcmp(name,kWaitForInitializeClass))return selected==Case::MissingWait?nullptr:reinterpret_cast<void*>(OriginalWait);
  return Hooks::FindSymbol(lib,name);
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  assert(stopped==1&&Read32(current)==(92u<<24));*original=target;
  if(target==reinterpret_cast<void*>(OriginalInitialize))initialize_proxy=reinterpret_cast<abi::arm64::android11::InitializeClass>(proxy);
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
 alignas(4096) std::array<unsigned char,512> original_thread{};std::memcpy(original_thread.data(),thread.data(),512);
 JNIEnv local_env{&table};current=original_thread.data();episode_env=&local_env;
 std::memcpy(original_thread.data()+216,&current,8);std::memcpy(original_thread.data()+200,&episode_env,8);
 alignas(8) unsigned char scope[32]{};OriginalHeader header{nullptr,1};std::memcpy(scope,&header,sizeof(header));
 if(sized_scope)Put32(scope,12,1);Put32(scope,OriginalReferences(),uint32_t(uintptr_t(type)));Publish(scope);Put32(current,0,92u<<24);
 (owner?owner_slot:waiter_slot).store(scope+OriginalReferences(),std::memory_order_release);
 if(!owner&&waiter_native(episode_env,reinterpret_cast<jclass>(parent_class))!=JNI_TRUE)return false;
 Put32(current,0,0);const bool result=initialize_proxy(&linker,current,{scope+OriginalReferences()},true,true);
 assert(Top()==scope);Publish(nullptr);return result;
}
jint EpisodeRun(JNIEnv* e,jclass type,jmethodID method,va_list args){
 if(method!=reinterpret_cast<jmethodID>(&run_episode_id))return StaticInt(e,type,method,args);
 Jni();bool fail=va_arg(args,int)!=0;auto* klass=fail?old_class:new_class;StoreStatus(klass,12);
 original_waits=0;original_bodies=0;bool owner_result=false,waiter_result=false;
 std::thread owner([&]{owner_result=OriginalThread(klass,true);});
 std::thread waiter([&]{waiter_result=OriginalThread(klass,false);});owner.join();waiter.join();
 const bool oracle=original_waits==1&&original_bodies==1&&owner_result==!fail&&waiter_result==!fail;
 return oracle&&selected!=Case::WrongPublicResult?1:0;
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
 const auto mutations_before=mutations,publications=retirement_publications;
 const bool ok=observer.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error);
 assert(ok==(selected==Case::Positive||selected==Case::MovingRoots));assert(observer.ReadyFor(metadata,roots)==ok);
 modern_field_metadata::FieldMetadata foreign=metadata;assert(!observer.ReadyFor(foreign,roots));
 assert(mutations==mutations_before&&retirement_publications==publications);
 if(ok){native_initializer_episode::Service wrong_owner;assert(!wrong_owner.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error));assert(observer.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),metadata,roots,&error));}
 if(ok&&selected==Case::Positive){
  native_initializer_episode::Witness absent;assert(!observer.CaptureCurrent(&env,&absent));
  witness_service=&observer;body_native=WitnessBody;StoreStatus(new_class,12);
  std::thread witness_owner([]{assert(OriginalThread(new_class,true));});witness_owner.join();
  modern_field_metadata::ReadContext outside{&observer,[](void*,const void*,size_t*out){*out=4;return true;}};
  assert(!observer.MatchesSourceClass(stale_witness,outside,new_class));witness_service=nullptr;
 }
 table=saved;
}
int main(){
 for(Case test:{Case::Positive,Case::MovingRoots,Case::MissingWait,Case::WrongWaitReturn,Case::WrongInitReturn,Case::WrongCompletion,
     Case::WrongPublicResult,Case::WrongBodyTid,Case::UnownedWaitHandle,Case::WrongWaitSelf}){
  pid_t child=fork();assert(child>=0);if(child==0){selected=test;int result=RootsFixtureMain();_exit(result);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 std::cout<<"Actual production init/wait observers + real calibrated roots + independent condition-variable source waiter normal/error/owner/return/handle negative episodes PASS\n";
}

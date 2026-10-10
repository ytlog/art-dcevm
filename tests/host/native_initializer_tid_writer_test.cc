#include "platform/common/native_initializer_tid_writer.h"
#include <atomic>
#include <stdexcept>
JavaVM* EpisodeVM();
artdcevm::abi::arm64::android11::ReleaseObjectPointer EpisodeGlobal(jobject);
void RunWriter(artdcevm::modern_field_metadata::FieldMetadata&,artdcevm::source_handle_roots::Service&);
#define main RootsFixtureMain
#include "episode_root_fixture.inc"
#undef main
namespace artdcevm::runnable_executor {bool Available(){return true;}}
#include "state_helpers.inc"
namespace {
int writer_type_id,writer_run_id,writer_oracle_id;
using WriterObserver=jboolean(*)(JNIEnv*,jclass,jclass,jint);
WriterObserver writer_observer=nullptr;
using namespace abi::arm64::android11::initializer_transaction;
using Offset=abi::arm64::android11::NativeOffset;
std::atomic<bool> transaction{false};bool monitor_owned=false;size_t records=0,setters=0;
uint32_t recorded_value=0;bool throws_record=false;
enum class WriterCase{Inline,Named,WrongRuntime,MissingQuery,MissingRecorder,Active,WrongNamedSetter,WrongPublicTid,WrongPublicResult,MonitorRefused};
WriterCase writer_case=WriterCase::Inline;
bool OriginalQuery(const void*p){assert(p==&runtime);return transaction.load();}
void OriginalRecord(void*p,void*object,Offset offset,uint32_t before,bool vol){
 assert(p==&runtime&&object&&offset.value==offsetof(class_metadata::Class17,clinit_thread_id_or_hash)&&!vol);
 assert(__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(object)+offset.value),__ATOMIC_RELAXED)==before);
 ++records;recorded_value=before;if(throws_record)throw std::runtime_error("Original transaction recorder failed");
}
void OriginalSetter(void*object,int32_t value){
 ++setters;if(writer_case==WriterCase::WrongNamedSetter)return;
 const Offset offset{offsetof(class_metadata::Class17,clinit_thread_id_or_hash)};
 auto*cell=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(object)+offset.value);
 if(OriginalQuery(&runtime))OriginalRecord(&runtime,object,offset,__atomic_load_n(cell,__ATOMIC_RELAXED),false);
 __atomic_store_n(cell,uint32_t(value),__ATOMIC_RELAXED);
}
struct WriterHooks:Hooks{
 void*FindSymbol(const char*lib,const char*name)override{
  if(!std::strcmp(name,"_ZN3art7Runtime9instance_E")){static void* actual=&runtime;return &actual;}
  if(!std::strcmp(name,kActive))return writer_case==WriterCase::MissingQuery?nullptr:reinterpret_cast<void*>(OriginalQuery);
  if(!std::strcmp(name,kRecordConst)||!std::strcmp(name,kRecord))return writer_case==WriterCase::MissingRecorder?nullptr:reinterpret_cast<void*>(OriginalRecord);
  if(!std::strcmp(name,kSetTid))return writer_case==WriterCase::Named||writer_case==WriterCase::WrongNamedSetter?reinterpret_cast<void*>(OriginalSetter):nullptr;
  return Hooks::FindSymbol(lib,name);
 }
};
jclass WriterFind(JNIEnv*e,const char*n){
 if(!std::strcmp(n,"com/github/ytlog/artdcevm/internal/InitializerWriterOracle")){Jni();return reinterpret_cast<jclass>(&writer_oracle_id);}
 return ProbeFind(e,n);
}
jmethodID WriterMethod(JNIEnv*e,jclass k,const char*n,const char*s){
 if(k==reinterpret_cast<jclass>(&writer_oracle_id)){
  Jni();if(!std::strcmp(n,"type")){assert(!std::strcmp(s,"()Ljava/lang/Class;"));return reinterpret_cast<jmethodID>(&writer_type_id);}
  assert(!std::strcmp(n,"run")&&!std::strcmp(s,"()I"));return reinterpret_cast<jmethodID>(&writer_run_id);
 }return ProbeMethod(e,k,n,s);
}
jint WriterRegister(JNIEnv*e,jclass k,const JNINativeMethod*m,jint n){
 if(k!=reinterpret_cast<jclass>(&writer_oracle_id))return Register(e,k,m,n);
 Jni();assert(n==1&&!std::strcmp(m->name,"observe")&&!std::strcmp(m->signature,"(Ljava/lang/Class;I)Z"));
 writer_observer=reinterpret_cast<WriterObserver>(m->fnPtr);return JNI_OK;
}
jobject WriterInactive(JNIEnv*e,jclass k,jmethodID id,va_list a){
 if(id!=reinterpret_cast<jmethodID>(&writer_type_id))return Inactive(e,k,id,a);
 Jni();new_class->status=12u<<28;new_class->clinit_thread_id_or_hash=0;return reinterpret_cast<jobject>(new_class);
}
jint WriterRun(JNIEnv*e,jclass k,jmethodID id,va_list a){
 if(id!=reinterpret_cast<jmethodID>(&writer_run_id))return Run(e,k,id,a);
 Jni();assert(writer_observer&&!monitor_owned);new_class->status=13u<<28;new_class->clinit_thread_id_or_hash=tid;
 const bool accepted=writer_observer(e,k,reinterpret_cast<jclass>(new_class),writer_case==WriterCase::WrongPublicTid?tid+1:tid)==JNI_TRUE;
 assert(!monitor_owned);if(!accepted){new_class->status=3u<<28;return 0;}
 assert(new_class->clinit_thread_id_or_hash==uint32_t(tid));new_class->status=14u<<28;
 return writer_case==WriterCase::WrongPublicResult?0:0x13718419;
}
jint WriterEnter(JNIEnv*,jobject object){Jni();assert(object==reinterpret_cast<jobject>(new_class)&&!monitor_owned);if(writer_case==WriterCase::MonitorRefused)return JNI_ERR;monitor_owned=true;return JNI_OK;}
jint WriterExit(JNIEnv*,jobject object){Jni();assert(object==reinterpret_cast<jobject>(new_class)&&monitor_owned);monitor_owned=false;return JNI_OK;}
void Protocol(){
 class_metadata::Class17 object{};object.clinit_thread_id_or_hash=731;
 transaction=true;records=0;throws_record=false;
 abi::arm64::android11::initializer_transaction::SetClinitThreadId(&runtime,&object,{offsetof(class_metadata::Class17,clinit_thread_id_or_hash)},913,OriginalQuery,OriginalRecord);
 assert(records==1&&recorded_value==731&&object.clinit_thread_id_or_hash==913);
 throws_record=true;bool threw=false;try{abi::arm64::android11::initializer_transaction::SetClinitThreadId(&runtime,&object,{offsetof(class_metadata::Class17,clinit_thread_id_or_hash)},1001,OriginalQuery,OriginalRecord);}catch(const std::runtime_error&){threw=true;}
 assert(threw&&records==2&&recorded_value==913&&object.clinit_thread_id_or_hash==913);
 throws_record=false;transaction=false;
}
void TestWriter(const modern_field_metadata::FieldMetadata&m,const source_handle_roots::Service&r,WriterCase selected){
 Protocol();writer_case=selected;variant=Variant::Dedicated;new_class->status=12u<<28;new_class->clinit_thread_id_or_hash=0;
 records=0;setters=0;transaction=selected==WriterCase::Active;
 table.FindClass=WriterFind;table.GetStaticMethodID=WriterMethod;table.RegisterNatives=WriterRegister;
 table.CallStaticObjectMethodV=WriterInactive;table.CallStaticIntMethodV=WriterRun;table.MonitorEnter=WriterEnter;table.MonitorExit=WriterExit;
 runtime_services::Snapshot observed;observed.runtime=selected==WriterCase::WrongRuntime?static_cast<void*>(old_class):static_cast<void*>(&runtime);observed.receivers.java_vm=EpisodeVM();observed.receivers.class_linker=old_class;
 WriterHooks hooks;native_initializer_tid_writer::Service service;std::string error;
 const bool ok=service.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_method),m,r,observed,&error);
 const bool expected=selected==WriterCase::Inline||selected==WriterCase::Named;
 if(ok!=expected)std::cerr<<"writer case "<<int(selected)<<" expected="<<expected<<" got="<<ok<<" error="<<error<<"\n";
 assert(ok==expected&&service.ReadyFor(m,r)==expected&&service.InactiveTransaction()==expected);
 assert(records==0&&!monitor_owned);
 if(selected==WriterCase::Named)assert(setters==2);
 if(selected==WriterCase::Inline)assert(setters==0);
 auto copied=m;assert(!service.ReadyFor(copied,r));
 if(expected){transaction=true;assert(!service.InactiveTransaction());transaction=false;assert(service.InactiveTransaction());}
}
}
jint WriterVMEnv(JavaVM*,void**out,jint version){assert(version==JNI_VERSION_1_6);*out=&env;return JNI_OK;}
JavaVM* EpisodeVM(){static JNIInvokeInterface_ methods=[] {JNIInvokeInterface_ value{};value.GetEnv=WriterVMEnv;return value;}();static JavaVM value{&methods};return &value;}
abi::arm64::android11::ReleaseObjectPointer EpisodeGlobal(jobject p){return {uintptr_t(p)};}
void RunWriter(modern_field_metadata::FieldMetadata&m,source_handle_roots::Service&r){
 for(auto selected:{WriterCase::Inline,WriterCase::Named,WriterCase::WrongRuntime,WriterCase::MissingQuery,WriterCase::MissingRecorder,WriterCase::Active,WriterCase::WrongNamedSetter,WriterCase::WrongPublicTid,WriterCase::WrongPublicResult,WriterCase::MonitorRefused}){
  pid_t child=fork();assert(child>=0);if(child==0){TestWriter(m,r,selected);_exit(0);}int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
}
int main(){int result=RootsFixtureMain();if(!result)std::cout<<"Original initializer transaction writer: real calibrated Class/roots/private producer and independent old-value log ordering/refund/monitor/transaction/owner negatives PASS\n";return result;}

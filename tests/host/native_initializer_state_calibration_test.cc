#include "native_initializer_state_fixture.inc"
#include "platform/common/native_initializer_state_calibration.h"
namespace {
using Observer=jboolean(*)(JNIEnv*,jclass,jclass,jint,jint);
Observer observer=nullptr;
enum class Variant{Dedicated,Shared,WrongTid,WrongClass,WrongStatus,Missing,Duplicate,WrongHash,WrongReentrantValue,ReentrantStateChange,ReentrantTidChange,WrongPoisonState,WrongPoisonResult};
Variant variant=Variant::Dedicated;
int inactive_id,run_id,reenter_id,failed_class_id,run_failure_id;
int reentries=0;
constexpr jint tid=731;
constexpr jint hash=static_cast<jint>(0xf4e19653u);
constexpr jint marker=0x18491315;
jclass ProbeFind(JNIEnv* e,const char* name){
 if(!std::strcmp(name,"com/github/ytlog/artdcevm/internal/InitializerStateOracle")){Jni();return reinterpret_cast<jclass>(parent_class);}
 return Find(e,name);
}
jmethodID ProbeMethod(JNIEnv* e,jclass klass,const char* name,const char* signature){
 if(klass==reinterpret_cast<jclass>(parent_class)){
  if(!std::strcmp(name,"inactiveClass")){assert(!std::strcmp(signature,"()Ljava/lang/Class;"));Jni();return reinterpret_cast<jmethodID>(&inactive_id);}
  if(!std::strcmp(name,"failedClass")){assert(!std::strcmp(signature,"()Ljava/lang/Class;"));Jni();return reinterpret_cast<jmethodID>(&failed_class_id);}
  if(!std::strcmp(name,"runFailure")){assert(!std::strcmp(signature,"()I"));Jni();return reinterpret_cast<jmethodID>(&run_failure_id);}
  if(!std::strcmp(name,"reenter")){assert(!std::strcmp(signature,"()I"));Jni();return reinterpret_cast<jmethodID>(&reenter_id);}
  if(!std::strcmp(name,"run")){assert(!std::strcmp(signature,"()I"));Jni();return reinterpret_cast<jmethodID>(&run_id);}
 }
 return StaticMethodId(e,klass,name,signature);
}
jint Register(JNIEnv*,jclass klass,const JNINativeMethod* methods,jint count){
 Jni();assert(klass==reinterpret_cast<jclass>(parent_class)&&count==1);
 assert(!std::strcmp(methods[0].name,"observe")&&!std::strcmp(methods[0].signature,"(Ljava/lang/Class;II)Z"));
 observer=reinterpret_cast<Observer>(methods[0].fnPtr);assert(observer);return JNI_OK;
}
jobject Inactive(JNIEnv*,jclass klass,jmethodID method,va_list){
 Jni();assert(klass==reinterpret_cast<jclass>(parent_class));
 if(method==reinterpret_cast<jmethodID>(&failed_class_id))return reinterpret_cast<jobject>(old_class);
 assert(method==reinterpret_cast<jmethodID>(&inactive_id));return reinterpret_cast<jobject>(new_class);
}
jint Run(JNIEnv* e,jclass klass,jmethodID method,va_list args){
 if(method==reinterpret_cast<jmethodID>(&run_failure_id)){
  Jni();assert(klass==reinterpret_cast<jclass>(parent_class));
  old_class->status=(variant==Variant::WrongPoisonState?14u:3u)<<28;
  return variant==Variant::WrongPoisonResult?2:1;
 }
 if(method==reinterpret_cast<jmethodID>(&reenter_id)){
  Jni();assert(klass==reinterpret_cast<jclass>(parent_class));
  assert((new_class->status>>28)==13&&new_class->clinit_thread_id_or_hash==uint32_t(tid));
  ++reentries;
  if(variant==Variant::ReentrantStateChange)new_class->status=14u<<28;
  if(variant==Variant::ReentrantTidChange)new_class->clinit_thread_id_or_hash=tid+1;
  return variant==Variant::WrongReentrantValue?marker:0;
 }
 if(method!=reinterpret_cast<jmethodID>(&run_id))return StaticInt(e,klass,method,args);
 Jni();assert(klass==reinterpret_cast<jclass>(parent_class)&&observer&&(new_class->status>>28)==12);
 // An independent source/public boundary executes the registered production
 // callback inside real initialization, before exposing the public body marker.
 new_class->status=(variant==Variant::WrongStatus?14u:13u)<<28;
 new_class->clinit_thread_id_or_hash=uint32_t(tid);
 bool ok=true;
 if(variant!=Variant::Missing)ok=observer(e,klass,reinterpret_cast<jclass>(variant==Variant::WrongClass?old_class:new_class),
     variant==Variant::WrongTid?tid+1:tid,hash)==JNI_TRUE;
 if(variant==Variant::Duplicate)ok=ok&&observer(e,klass,reinterpret_cast<jclass>(new_class),tid,hash)==JNI_TRUE;
 if(!ok){new_class->status=3u<<28;return 0;}
 new_class->status=14u<<28;
 if(variant==Variant::Shared||variant==Variant::WrongHash)
  new_class->clinit_thread_id_or_hash=~uint32_t(variant==Variant::WrongHash?hash+1:hash);
 return marker;
}
void Test(const modern_field_metadata::FieldMetadata& metadata,Variant selected){
 variant=selected;new_class->status=12u<<28;new_class->clinit_thread_id_or_hash=0;
 native_initializer_state::Calibration calibration;std::string error;
 const int writes=mutations,publications=retirement_publications;
 const bool ok=calibration.Calibrate(&env,reinterpret_cast<jobject>(old_method),metadata,&error);
 assert(mutations==writes&&retirement_publications==publications);
 const bool expected=selected==Variant::Dedicated||selected==Variant::Shared;
 assert(ok==expected&&calibration.CalibratedFor(metadata)==expected);
 native_initializer_state::State state{99,99,99};
 modern_field_metadata::ReadContext context{&heap,HeapExtent};
 if(!expected){assert(!calibration.Read(metadata,new_class,context,&state));assert(state.status==99);return;}
 assert(reentries==1);
 assert(calibration.Storage()==(selected==Variant::Shared?native_initializer_state::TidStorage::kDescriptorHashShared:native_initializer_state::TidStorage::kDedicated));
 assert(calibration.Read(metadata,new_class,context,&state)&&state.status==14);
 assert(state.initializing_tid==(selected==Variant::Shared?0u:uint32_t(tid)));
 new_class->status=13u<<28;new_class->clinit_thread_id_or_hash=tid;
 assert(calibration.Read(metadata,new_class,context,&state)&&state.initializing_tid==uint32_t(tid));
 new_class->clinit_thread_id_or_hash=0;state={99,99,99};
 assert(!calibration.Read(metadata,new_class,context,&state)&&state.status==99);
 if(selected==Variant::Shared){new_class->clinit_thread_id_or_hash=~uint32_t(hash);
  assert(!calibration.Read(metadata,new_class,context,&state)&&state.status==99);}
 modern_field_metadata::FieldMetadata copied=metadata;
 assert(!calibration.CalibratedFor(copied)&&!calibration.Read(copied,new_class,context,&state));
 auto invalid=context;invalid.owner=nullptr;assert(!calibration.Read(metadata,new_class,invalid,&state));
}
}
int main(){
 InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};
 for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Request{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracle;std::string* error;} request{&metadata,oracle.data(),&error};
 const auto prepare=[](const runnable_jni::Context& context,void* data){auto& p=*static_cast<Request*>(data);
  return p.metadata->Calibrate(context,reinterpret_cast<jobject>(old_class),"LFixture;",p.oracle,4,p.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_method),prepare,&request)==runnable_jni::Result::kSuccess);
 table.FindClass=ProbeFind;table.GetStaticMethodID=ProbeMethod;table.RegisterNatives=Register;
 table.CallStaticObjectMethodV=Inactive;table.CallStaticIntMethodV=Run;
 for(Variant selected:{Variant::Dedicated,Variant::Shared,Variant::WrongTid,Variant::WrongClass,
     Variant::WrongStatus,Variant::Missing,Variant::Duplicate,Variant::WrongHash,
     Variant::WrongReentrantValue,Variant::ReentrantStateChange,Variant::ReentrantTidChange,
     Variant::WrongPoisonState,Variant::WrongPoisonResult}){
  // Each fork represents a fresh one-shot private class/runtime episode. There
  // is no attempted reinitialization of a live real Java Class in the module.
  pid_t child=fork();assert(child>=0);if(child==0){Test(metadata,selected);_exit(0);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 std::cout<<"Production clinit JNI observer: dedicated/shared slot, registered production callback episode, real producer reentrant source boundary, wrong tid/class/state/hash/reentry and no final fallback PASS\n";
}

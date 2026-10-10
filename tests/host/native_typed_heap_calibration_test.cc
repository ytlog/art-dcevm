#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_typed_heap_walk_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_typed_heap_calibration.h"
#include <sys/wait.h>
namespace {
struct Count {jobject value=nullptr;int locals=0,globals=0;};
std::array<Count,32> owned{};bool pending=false,missing=false,duplicate=false,array_exception=false,same_exception=false;
int global_call=0,fail_global=0,set_call=0,fail_set=0,local_call=0,fail_local=0;
Count& Account(jobject value){for(auto& c:owned)if(c.value==value)return c;for(auto& c:owned)if(!c.value){c.value=value;return c;}std::abort();}
jobject Local(jobject value){++local_call;if(local_call==fail_local){pending=true;return nullptr;}if(value)++Account(value).locals;return value;}
jobject GlobalOwned(JNIEnv*,jobject value){Jni();if(++global_call==fail_global){pending=true;return nullptr;}if(value)++Account(value).globals;return value;}
void DeleteLocalOwned(JNIEnv*,jobject value){Jni();assert(value&&Account(value).locals>0);--Account(value).locals;}
void DeleteGlobalOwned(JNIEnv*,jobject value){Jni();assert(value&&Account(value).globals>0);--Account(value).globals;}
void Empty(){for(const auto& c:owned)assert(!c.locals&&!c.globals);}
jboolean Pending(JNIEnv*){Jni();return pending;}
jclass ClassOwned(JNIEnv*,jobject o){Jni();return static_cast<jclass>(Local(reinterpret_cast<jobject>(Root(o))));}
jclass FindOwned(JNIEnv*,const char* name){Jni();assert(!std::strcmp(name,"java/lang/Object"));return static_cast<jclass>(Local(reinterpret_cast<jobject>(parent_class)));}
jfieldID InstanceId(JNIEnv*,jclass c,const char* name,const char* type){Jni();assert(c==reinterpret_cast<jclass>(old_class)&&!std::strcmp(name,"referenceValue")&&!std::strcmp(type,"Ljava/lang/Object;"));return reinterpret_cast<jfieldID>(old_fields[2]);}
jfieldID StaticId(JNIEnv*,jclass c,const char* name,const char* type){Jni();assert(c==reinterpret_cast<jclass>(old_class)&&!std::strcmp(name,"staticReferenceValue")&&!std::strcmp(type,"Ljava/lang/Object;"));return reinterpret_cast<jfieldID>(old_fields[0]);}
jobject Reflected(JNIEnv*,jclass c,jfieldID id,jboolean statik){Jni();assert(c==reinterpret_cast<jclass>(old_class));assert(id==reinterpret_cast<jfieldID>(old_fields[statik?0:2]));return Local(reinterpret_cast<jobject>(&mirrors[statik?0:2]));}
jobject InstanceValue(JNIEnv*,jobject o,jfieldID id){Jni();assert(o==reinterpret_cast<jobject>(old_instance)&&id==reinterpret_cast<jfieldID>(old_fields[2]));return Local(missing?nullptr:reinterpret_cast<jobject>(old_instance));}
jobject StaticValue(JNIEnv*,jclass o,jfieldID id){Jni();assert(o==reinterpret_cast<jclass>(old_class)&&id==reinterpret_cast<jfieldID>(old_fields[0]));return Local(reinterpret_cast<jobject>(duplicate?old_instance:new_instance));}
jobjectArray ArrayNew(JNIEnv*,jsize n,jclass type,jobject initial){Jni();assert(n==2&&type==reinterpret_cast<jclass>(parent_class)&&!initial);fixture_array=heap.New(20);Put32(fixture_array,8,2);auto result=Local(reinterpret_cast<jobject>(fixture_array));if(array_exception)pending=true;return static_cast<jobjectArray>(result);}
void ArraySet(JNIEnv*,jobjectArray a,jsize i,jobject value){Jni();assert(a==fixture_array&&i>=0&&i<2);if(++set_call==fail_set){pending=true;return;}Put32(a,12+4*size_t(i),uint32_t(uintptr_t(value)));}
jobject ArrayOwnedGet(JNIEnv*,jobjectArray a,jsize i){return Local(ArrayGet(&env,a,i));}
jboolean SameOwned(JNIEnv*,jobject a,jobject b){Jni();if(same_exception)pending=true;return a==b;}
void Wrappers(){table.IsSameObject=SameOwned;table.ExceptionCheck=Pending;table.NewGlobalRef=GlobalOwned;table.DeleteGlobalRef=DeleteGlobalOwned;table.DeleteLocalRef=DeleteLocalOwned;table.GetObjectClass=ClassOwned;table.FindClass=FindOwned;table.GetFieldID=InstanceId;table.GetStaticFieldID=StaticId;table.ToReflectedField=Reflected;table.GetObjectField=InstanceValue;table.GetStaticObjectField=StaticValue;table.GetArrayLength=ArrayLength;table.GetObjectArrayElement=ArrayOwnedGet;table.NewObjectArray=ArrayNew;table.SetObjectArrayElement=ArraySet;}
}
int main(){
 InitJni();InitializeHeap();types[0]="Ljava/lang/Object;";Put32(old_instance,12,uint32_t(uintptr_t(old_instance)));Put32(old_class,128,uint32_t(uintptr_t(new_instance)));
 TypedHooks hooks;std::string error;exclusive_owned_roots::Service exclusive;assert(exclusive_owned_roots::Service::Bind(hooks,&exclusive,&error));
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 const auto cal=[](const runnable_jni::Context& c,void* raw){auto& p=*static_cast<std::pair<modern_field_metadata::FieldMetadata*,std::array<modern_field_metadata::FieldOracle,4>*>*>(raw);return p.first->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",p.second->data(),4,nullptr);};
 std::pair<modern_field_metadata::FieldMetadata*,decltype(oracle)*> request{&metadata,&oracle};assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),cal,&request)==runnable_jni::Result::kSuccess);
 Wrappers();
 // Each negative process exercises the actual helper and sole native observer,
 // without replacing production calibration with a mock-ready flag.
 for(int fault=0;fault<22;++fault){const pid_t child=fork();assert(child>=0);if(child==0){
  Service service;
  if(fault==0)pending=true;else if(fault==1)missing=true;else if(fault==2)duplicate=true;
  else if(fault==3)array_exception=true;else if(fault>=4&&fault<=10)fail_global=fault-3;
  else if(fault==11||fault==12)fail_set=fault-10;
  else if(fault>=13&&fault<=19)fail_local=fault-12;else if(fault==20)hide_fields=true;else same_exception=true;
  assert(!native_typed_heap_calibration::Prepare(hooks,&env,reinterpret_cast<jobject>(old_instance),expected_heap,Current,metadata,exclusive,&service,&error));
  assert(!service.Calibrated());Empty();if(fault==0||fault==3||(fault>=4&&fault<=19)||fault==21)assert(pending);_exit(0);
 }int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
 Service service;assert(native_typed_heap_calibration::Prepare(hooks,&env,reinterpret_cast<jobject>(old_instance),expected_heap,Current,metadata,exclusive,&service,&error));
 assert(service.Calibrated()&&!pending);Empty();const auto created=global_call;
 assert(native_typed_heap_calibration::Prepare(hooks,&env,reinterpret_cast<jobject>(old_instance),expected_heap,Current,metadata,exclusive,&service,&error)&&global_call==created);Empty();
 std::cout<<"Actual typed heap Prepare helper: JNI fixture/array/field source calibration, missing/duplicate values, pending exception preservation, all globals/local failures and idempotence PASS\n";
}

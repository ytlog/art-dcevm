#include "platform/common/native_source_commit.h"
#include "platform/common/modern_source_commit.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android-common/closure.h"
#include "platform/common/object_replacement_map_abi.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace artdcevm;
namespace {
// The test VM owns a real low-address heap so compressed roots are meaningful.
// JNI and named ART callees are boundary doubles. Calibrated services and the
// complete native adapter are production implementations, not injected flags.
struct Heap {
 unsigned char* base;size_t used=0;
 Heap(){base=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),1<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(base!=MAP_FAILED&&uintptr_t(base)+(1<<20)<=UINT32_MAX);}
 ~Heap(){munmap(base,1<<20);}
 void* New(size_t bytes){used=(used+15)&~size_t{15};assert(used+bytes<(1<<20));auto* p=base+used;used+=bytes;return p;}
} heap;
uint32_t Read32(const void* p,size_t at=0){uint32_t n;std::memcpy(&n,static_cast<const char*>(p)+at,4);return n;}
void Put32(void* p,size_t at,uint32_t n){std::memcpy(static_cast<char*>(p)+at,&n,4);}
void* Root(void*);
void* Mark(void*);
alignas(8) std::array<unsigned char,200> thread{},foreign_thread{};
void* current=thread.data();int thread_list,mutex,linker,runtime,instrumentation,jit,jit_cache;
void* list_lock=&mutex;
int stopped=0,locked=0,cleared=0,mutations=0,jni_calls=0;
std::vector<std::string> events;
uint8_t* cards=static_cast<uint8_t*>(heap.New(2048));
uintptr_t bias=uintptr_t(cards)-(uintptr_t(heap.base)>>10);
class_metadata::Class17* old_class=nullptr;class_metadata::Class17* new_class=nullptr;
void* parent_class=nullptr;void* meta_class=nullptr;void* old_instance=nullptr;void* new_instance=nullptr;
void* ref_a=nullptr;void* ref_b=nullptr;void* class_ext=nullptr;void* class_array=nullptr;
void* moved_class=nullptr;void* moved_ext=nullptr;bool replaced=false,wrong_readback=false;int retain_pipe=-1;
std::array<abi::arm64::Field*,4> old_fields{},new_fields{};
std::array<const char*,4> names{"number","value","left","right"};
std::array<const char*,4> types{"I","I","Ljava/lang/Object;","Ljava/lang/Object;"};
struct Mirror {void* field;};std::array<Mirror,4> mirrors{};
void* Root(void* p){if(wrong_readback&&replaced&&p==old_fields[0])return new_class;return reinterpret_cast<void*>(uintptr_t(Read32(p)));}
void* Mark(void* p){return replaced&&p==new_class?moved_class:p;}
const char* FieldName(void* field){for(size_t i=0;i<4;++i)if(old_fields[i]==field||new_fields[i]==field)return names[i];return nullptr;}
const char* FieldType(void* field){for(size_t i=0;i<4;++i)if(old_fields[i]==field||new_fields[i]==field)return types[i];return nullptr;}
void* FieldLeaf(void* mirror){assert(!stopped&&Read32(current)==0);return static_cast<Mirror*>(mirror)->field;}
const char* Descriptor(void* klass,void*){return klass==parent_class?"Ljava/lang/Object;":"LFixture;";}
void* Current(){return current;}
void Stop(void*,const char*,bool){assert(!stopped&&!locked);++stopped;}
void Resume(void*){assert(stopped==1&&!locked);--stopped;}
abi::arm64::android11::ReleaseObjectPointer Decode(const void*,jobject p){return {uintptr_t(p)};}
void Jni(){assert(!stopped);++jni_calls;}
jboolean Exception(JNIEnv*){Jni();return false;}
jthrowable ExceptionOccurred(JNIEnv*){Jni();return nullptr;}
void VoidException(JNIEnv*){Jni();}
jobjectRefType RefType(JNIEnv*,jobject){Jni();return JNIGlobalRefType;}
jobject Global(JNIEnv*,jobject p){Jni();return p;}
void Delete(JNIEnv*,jobject){Jni();}
jboolean SameObject(JNIEnv*,jobject a,jobject b){Jni();return a==b;}
jclass ObjectClass(JNIEnv*,jobject o){Jni();return reinterpret_cast<jclass>(Root(o));}
jclass Find(JNIEnv*,const char* name){Jni();return reinterpret_cast<jclass>(std::strcmp(name,"java/lang/Class")==0?meta_class:parent_class);}
int superclass_id,loader_id,gc_id,hash_id;
jmethodID MethodId(JNIEnv*,jclass,const char* name,const char*){Jni();return reinterpret_cast<jmethodID>(std::strcmp(name,"getSuperclass")==0?&superclass_id:&loader_id);}
jmethodID StaticMethodId(JNIEnv*,jclass,const char* name,const char*){Jni();return reinterpret_cast<jmethodID>(std::strcmp(name,"gc")==0?&gc_id:&hash_id);}
jobject ObjectMethod(JNIEnv*,jobject o,jmethodID id,va_list){Jni();assert(o);return id==reinterpret_cast<jmethodID>(&superclass_id)?reinterpret_cast<jobject>(parent_class):nullptr;}
jfieldID FieldId(JNIEnv*,jobject mirror){Jni();const auto state=Read32(current);Put32(current,0,0);void* field=FieldLeaf(mirror);Put32(current,0,state);return reinterpret_cast<jfieldID>(field);}
jint GetInt(JNIEnv*,jobject owner,jfieldID id){Jni();return static_cast<jint>(Read32(owner,reinterpret_cast<abi::arm64::Field*>(id)->offset));}
jint GetStaticInt(JNIEnv* e,jclass owner,jfieldID id){return GetInt(e,owner,id);}
void SetInt(JNIEnv*,jobject owner,jfieldID id,jint n){Jni();Put32(owner,reinterpret_cast<abi::arm64::Field*>(id)->offset,uint32_t(n));}
void SetStaticInt(JNIEnv* e,jclass owner,jfieldID id,jint n){SetInt(e,owner,id,n);}
jobject GetObject(JNIEnv*,jobject owner,jfieldID id){Jni();return reinterpret_cast<jobject>(uintptr_t(Read32(owner,reinterpret_cast<abi::arm64::Field*>(id)->offset)));}
void SetObject(JNIEnv*,jobject owner,jfieldID id,jobject value){Jni();assert(uintptr_t(value)<=UINT32_MAX);Put32(owner,reinterpret_cast<abi::arm64::Field*>(id)->offset,uint32_t(uintptr_t(value)));cards[(uintptr_t(owner)-uintptr_t(heap.base))>>10]=0x70;}
jobject Alloc(JNIEnv*,jclass owner){Jni();void* p=heap.New(32);Put32(p,0,uint32_t(uintptr_t(owner)));return reinterpret_cast<jobject>(p);}
void StaticVoid(JNIEnv*,jclass,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&gc_id));std::memset(cards,0,2048);}
jint StaticInt(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&hash_id));auto o=va_arg(args,jobject);uint32_t value=Read32(o,4)&~0x30000000u;if(!value){value=0x01000123;Put32(o,4,value);}return jint(value);}
jint Monitor(JNIEnv*,jobject){Jni();return JNI_OK;}
JNINativeInterface_ table{};JNIEnv env{&table};
void InitJni(){
 table.ExceptionCheck=Exception;table.ExceptionOccurred=ExceptionOccurred;table.ExceptionClear=VoidException;
 table.GetObjectRefType=RefType;table.NewGlobalRef=Global;table.DeleteGlobalRef=Delete;table.DeleteLocalRef=Delete;
 table.IsSameObject=SameObject;table.GetObjectClass=ObjectClass;table.FindClass=Find;table.GetMethodID=MethodId;table.GetStaticMethodID=StaticMethodId;
 table.CallObjectMethodV=ObjectMethod;table.FromReflectedField=FieldId;table.GetIntField=GetInt;table.SetIntField=SetInt;table.GetStaticIntField=GetStaticInt;table.SetStaticIntField=SetStaticInt;
 table.GetObjectField=GetObject;table.SetObjectField=SetObject;table.AllocObject=Alloc;table.CallStaticVoidMethodV=StaticVoid;table.CallStaticIntMethodV=StaticInt;table.MonitorEnter=Monitor;table.MonitorExit=Monitor;
}
using VendorLock=void(*)(void*,void*,uint32_t);VendorLock lock_proxy=nullptr;
constexpr uint32_t lock_argument=0xc1011234u;
void Vendor(void* lock,void* self,uint32_t arg){assert(lock==&mutex&&self==thread.data()&&arg==lock_argument);if(stopped){assert(!locked);++locked;}}
void Unlock(void* lock,void* self){assert(stopped&&locked==1&&lock==&mutex&&self==thread.data());--locked;}
void CachePrepare(){assert(!stopped);lock_proxy(&mutex,current,lock_argument);}
struct Closure:art::Closure {void Run(art::Thread*)override{assert(stopped&&locked);++cleared;events.push_back("cache");}} closure;
void ForEach(void* receiver,void(*action)(void*,void*),void* data){assert(receiver==&thread_list&&stopped&&locked);action(thread.data(),data);action(foreign_thread.data(),data);}
abi::arm64::android12::Method12* old_method=nullptr;abi::arm64::android12::Method12* next_method=nullptr;
void SetStatus(void* klass,uint8_t status){assert(stopped&&klass==new_class&&status==15);++mutations;events.push_back("status");Put32(klass,offsetof(class_metadata::Class17,status),uint32_t(status)<<28);}
void Retain(void* ext,void* old){assert(stopped&&ext==moved_ext&&old==old_class);assert(Root(old_method)==old_class);for(auto* field:old_fields)assert(Root(field)==old_class);if(retain_pipe>=0){const char marker=1;assert(write(retain_pipe,&marker,1)==1);}events.push_back("retain");}
void Invalidate(void* cache){assert(stopped&&cache==&jit_cache);events.push_back("invalidate");}
void Deopt(void* receiver){assert(stopped&&receiver==&instrumentation);events.push_back("deopt");}
void DeoptAll(void* receiver,const char* why){assert(stopped&&receiver==&instrumentation&&!std::strcmp(why,"artdcevm-class-abstract"));events.push_back("abstract");}
int binding_value;
void* MethodLeaf(const abi::arm64::android_common::RunnableJniScope&,jobject p){assert(!stopped&&Read32(current)==0);return p==reinterpret_cast<jobject>(old_method)?old_method:nullptr;}
bool SameMethod(void*,void*){return true;}
void Obsolete(void* receiver,void* method){assert(stopped&&receiver==&linker&&method==old_method);events.push_back("obsolete");}
bool Retired(void*){return false;}
void Replace(void* self,const void* raw){
 assert(stopped&&self==thread.data());events.push_back("replace");replaced=true;
 const auto& map=*static_cast<const object_replacement::object_map_abi::Map*>(raw);assert(map.size==2);
 unsigned matched=0;for(auto* entry=map.sentinel.next;entry;entry=entry->next){auto* node=static_cast<const object_replacement::object_map_abi::Node*>(entry);if(node->key==old_class){assert(node->value==new_class);matched|=1;}else{assert(node->key==old_instance&&node->value==new_instance);matched|=2;}}assert(matched==3);
 old_method->declaring_class=uint32_t(uintptr_t(new_class));for(auto* field:old_fields)field->declaring_class=uint32_t(uintptr_t(new_class));
 // Holder's array root must be read after replacement, not cached beforehand.
 Put32(class_array,12,uint32_t(uintptr_t(new_class)));
}
class Hooks:public HookEngine {
 public:const char* missing=nullptr;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(missing&&std::strstr(name,missing))return nullptr;
#define CALLEE(part,fn) if(std::strstr(name,part))return reinterpret_cast<void*>(fn)
  CALLEE("GetArtFieldEv",FieldLeaf);CALLEE("ArtField7GetNameEv",FieldName);CALLEE("ArtField17GetTypeDescriptorEv",FieldType);CALLEE("Class13GetDescriptor",Descriptor);
  CALLEE("artReadBarrierMark",Mark);CALLEE("artReadBarrierForRootSlow",Root);
  CALLEE("CurrentFromGdbEv",Current);CALLEE("Thread14CurrentFromGdbEv",Current);CALLEE("Thread13DecodeJObject",Decode);
  if(!std::strcmp(name,abi::arm64::android11::kCurrentThread))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendConstruct))return reinterpret_cast<void*>(Stop);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);
  if(std::strstr(name,"E7closure"))return &closure;
  CALLEE("ClearAllInterpreterCachesEv",CachePrepare);
  if(std::strstr(name,"thread_list_lock_E"))return &list_lock;
  CALLEE("ExclusiveLockEPNS_6ThreadEj",Vendor);CALLEE("ExclusiveUnlock",Unlock);CALLEE("ThreadList7ForEach",ForEach);
  CALLEE("SetStatusLocked",SetStatus);CALLEE("SetObsoleteClass",Retain);CALLEE("InvalidateAllCompiledCode",Invalidate);CALLEE("DeoptimizeAllThreadFrames",Deopt);CALLEE("DeoptimizeEverything",DeoptAll);CALLEE("ReplaceReferences",Replace);
  CALLEE("HasSameNameAndSignature",SameMethod);CALLEE("SetEntryPointsForObsoleteMethod",Obsolete);
#undef CALLEE
  return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{assert(stopped&&target==reinterpret_cast<void*>(Vendor));*original=target;lock_proxy=reinterpret_cast<VendorLock>(proxy);return this;}
 bool Remove(void*,std::string*)override{std::abort();}
};
bool reflection_ready=true;
int retirement_publications=0;
void PublishRetirement(const source_commit::Plan&,void*){assert(stopped&&reflection_ready);++retirement_publications;events.push_back("retire");}
bool ReflectiveReady(const source_commit::Plan&,void*){return reflection_ready;}
void ReflectiveUpdate(const source_commit::Plan&,void*){assert(stopped);events.push_back("reflective");}
void InitializeClass(class_metadata::Class17* c){c->object.klass=uint32_t(uintptr_t(meta_class));c->super_class=uint32_t(uintptr_t(parent_class));c->status=15u<<28;c->ext_data=uint32_t(uintptr_t(class_ext));}
void MakeFields(class_metadata::Class17* klass,std::array<abi::arm64::Field*,4>& output){auto* raw=static_cast<unsigned char*>(heap.New(4+4*16));Put32(raw,0,4);klass->fields=uintptr_t(raw);for(size_t i=0;i<4;++i){output[i]=reinterpret_cast<abi::arm64::Field*>(raw+4+16*i);*output[i]={uint32_t(uintptr_t(klass)),i==0?8u:1u,uint32_t(i),i==0?128u:uint32_t(4+4*i)};}}
void InitializeHeap(){
 meta_class=heap.New(160);parent_class=heap.New(160);class_ext=heap.New(16);
 old_class=static_cast<class_metadata::Class17*>(heap.New(160));new_class=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(old_class);InitializeClass(new_class);
 auto* parent=static_cast<class_metadata::Class17*>(parent_class);parent->object.klass=uint32_t(uintptr_t(meta_class));parent->status=15u<<28;
 MakeFields(old_class,old_fields);MakeFields(new_class,new_fields);
 old_instance=heap.New(32);new_instance=heap.New(32);ref_a=heap.New(16);ref_b=heap.New(16);Put32(old_instance,0,uint32_t(uintptr_t(old_class)));Put32(new_instance,0,uint32_t(uintptr_t(new_class)));
 Put32(old_class,128,73);Put32(old_instance,8,31);Put32(old_instance,12,uint32_t(uintptr_t(ref_a)));Put32(old_instance,16,uint32_t(uintptr_t(ref_b)));
 for(size_t i=0;i<4;++i)mirrors[i]={old_fields[i]};
 auto make_method=[&](class_metadata::Class17* c){auto* array=static_cast<unsigned char*>(heap.New(40));Put32(array,0,1);c->methods=uintptr_t(array);auto* method=reinterpret_cast<abi::arm64::android12::Method12*>(array+8);method->declaring_class=uint32_t(uintptr_t(c));method->access_flags=0x101;method->dex_method_index=4;method->data=uintptr_t(&binding_value);return method;};
 old_method=make_method(old_class);next_method=make_method(new_class);
 moved_class=heap.New(160);std::memcpy(moved_class,new_class,160);moved_ext=heap.New(16);Put32(moved_class,offsetof(class_metadata::Class17,ext_data),uint32_t(uintptr_t(moved_ext)));
 class_array=heap.New(16);Put32(class_array,4,1u<<28);Put32(class_array,8,1);Put32(class_array,12,uint32_t(uintptr_t(new_class)));
 Put32(thread.data(),0,92u<<24);std::memcpy(thread.data()+144,&bias,8);
}
}
// Host VM's verified JNI callback boundary. Production exclusive-owned-roots
// checks its Native/Runnable transition and independently reloads strong roots.
namespace artdcevm::runnable_executor {
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject p,void** out){if(!p||!out)return false;*out=p;return true;}
runnable_jni::Result With(JNIEnv* e,jobject,runnable_jni::Operation fn,void* data){assert(!stopped&&current==thread.data());Put32(thread.data(),0,0);bool ok=fn({current,e,&runtime},data);Put32(thread.data(),0,92u<<24);return ok?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}
int main(){
 InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct MetadataRequest{modern_field_metadata::FieldMetadata* service;const modern_field_metadata::FieldOracle* oracle;std::string* error;} calibration{&metadata,oracle.data(),&error};
 const auto calibrate=[](const runnable_jni::Context& context,void* data){auto& r=*static_cast<MetadataRequest*>(data);return r.service->Calibrate(context,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&calibration)==runnable_jni::Result::kSuccess);assert(metadata.StorageModel()==modern_field_metadata::Model::kMerged);
 modern_field_storage::Service fields;
 if(!fields.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),oracle.size(),&error)){std::cerr<<error<<'\n';return 1;}
 assert(fields.Calibrated()&&fields.CardTableOffset()==144&&Read32(old_instance,8)==31);
 native_cache::Service caches;assert(caches.Bind(hooks,&thread_list,&error)&&caches.Prepare(&error));
 method_identity::Oracle native{reinterpret_cast<jobject>(old_method),4,0x101};
 struct BindingRequest{Hooks* hooks;method_identity::Oracle native;std::string* error;std::unique_ptr<modern_methods::Binding> result;} binding_request{&hooks,native,&error,nullptr};
 const auto bind=[](const runnable_jni::Context& context,void* data){auto& r=*static_cast<BindingRequest*>(data);r.result=modern_methods::Service::BindOwned(*r.hooks,context,MethodLeaf,r.native,&binding_value,&linker,r.error);return bool(r.result);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),bind,&binding_request)==runnable_jni::Result::kSuccess);auto binding=std::move(binding_request.result);
 Put32(old_instance,4,0x01004567u);Put32(new_instance,4,0x20000001u);
 const int setup_jni=jni_calls;Stop(nullptr,nullptr,false);
 method_array::View before,after;const method_array::Boundaries boundaries{0,1,method_array::Order::ByFlags};
 assert(method_array::Read(method_metadata::kAndroid12Onward,reinterpret_cast<void*>(old_class->methods),40,boundaries,&before));assert(method_array::Read(method_metadata::kAndroid12Onward,reinterpret_cast<void*>(new_class->methods),40,boundaries,&after));
 auto methods=modern_methods::Service::PrepareTables(*binding,old_class,before,new_class,after,Retired,&error);assert(methods);
 source_commit::Plan plan{old_class,class_array,{old_class},{new_class},{old_instance},{new_instance},true};
 native_source_commit::Diagnostics diagnostics;
 native_source_commit::Dependencies deps;deps.diagnostics=&diagnostics;deps.metadata=&metadata;deps.fields=&fields;deps.caches=&caches;deps.reflective={ReflectiveReady,ReflectiveUpdate,nullptr,PublishRetirement};deps.observed.runtime=&runtime;deps.observed.instrumentation=&instrumentation;deps.observed.receivers.thread_list=&thread_list;deps.observed.receivers.jit=&jit;deps.observed.receivers.jit_code_cache=&jit_cache;
 auto prepare=[&](){return modern_source_commit::Prepare(hooks,deps,plan,{methods.get()},&error);};
 const int original_mutations=mutations;
 const std::vector<unsigned char> unchanged_heap(heap.base,heap.base+heap.used);
 const auto unchanged=[&](){return retirement_publications==0&&mutations==original_mutations&&!std::memcmp(unchanged_heap.data(),heap.base,unchanged_heap.size());};
 auto saved=deps.observed.receivers.jit;deps.observed.receivers.jit=nullptr;assert(!prepare()&&unchanged());deps.observed.receivers.jit=saved;
 modern_field_metadata::FieldMetadata uncalibrated_metadata;auto* metadata_saved=deps.metadata;deps.metadata=&uncalibrated_metadata;assert(!prepare()&&unchanged());deps.metadata=metadata_saved;
 modern_field_storage::Service uncalibrated_fields;auto* fields_saved=deps.fields;deps.fields=&uncalibrated_fields;assert(!prepare()&&unchanged());deps.fields=fields_saved;
 reflection_ready=false;assert(!prepare()&&unchanged());reflection_ready=true;
 auto* jit_saved=deps.observed.receivers.jit;auto* cache_saved=deps.observed.receivers.jit_code_cache;
 deps.observed.receivers.jit=deps.observed.receivers.jit_code_cache=nullptr;assert(!prepare()&&unchanged());
 deps.jit_absence_proven=true;assert(prepare()&&unchanged());deps.jit_absence_proven=false;deps.observed.receivers.jit=jit_saved;deps.observed.receivers.jit_code_cache=cache_saved;
 auto invalid=deps;invalid.native_roots=native_source_commit::NativeRootPolicy::kPreserve;
 assert(!native_source_commit::Services::Prepare(hooks,invalid,plan,{{old_class,new_class,methods.get(),nullptr}},&error)&&unchanged());
 invalid.native_roots=native_source_commit::NativeRootPolicy::kRestore;
 assert(!native_source_commit::Services::Prepare(hooks,invalid,plan,{{old_class,new_class,methods.get(),nullptr}},&error)&&unchanged());
 hooks.missing="ReplaceReferences";assert(!prepare()&&unchanged());hooks.missing=nullptr;
 current=foreign_thread.data();assert(!prepare()&&unchanged());current=thread.data();
 auto service=prepare();assert(service&&service->Validate(plan));source_commit::Plan copied=plan;assert(!service->Validate(copied));
 current=foreign_thread.data();assert(!source_commit::Run(plan,*service)&&unchanged());current=thread.data();
 // Prepare succeeded, but the independent reflective oracle rejects at Run.
 // No registry publication, field/status mutation or heap write may occur.
 reflection_ready=false;assert(!source_commit::Run(plan,*service)&&unchanged());reflection_ready=true;
 assert(retirement_publications==0);
 new_class->status=14u<<28;events.clear();int previous_jni=jni_calls;assert(previous_jni==setup_jni);assert(source_commit::Run(plan,*service));Resume(nullptr);
 assert(retirement_publications==1);
 assert(diagnostics.declaring_roots_verified&&diagnostics.cleared_threads==2);
 assert(jni_calls==previous_jni&&cleared==2&&Read32(new_class,128)==73&&Read32(old_class,128)==0&&Read32(new_instance,8)==31&&Read32(old_instance,8)==0);
 assert(Read32(new_instance,4)==0x01004567u&&Read32(old_instance,4)==0);
 assert(Root(old_method)==old_class&&next_method->data==uintptr_t(&binding_value));assert(Read32(new_instance,12)==uintptr_t(ref_a)&&Read32(new_instance,16)==uintptr_t(ref_b));
 const std::vector<std::string> expected{"retire","status","obsolete","reflective","deopt","replace","retain","invalidate","abstract","cache","cache"};assert(events==expected);
 // A named GC-root readback disagreeing after restoration must stop before
 // RetainOldClass. Test the real invariant rather than replacing the adapter.
 int pipes[2];assert(pipe(pipes)==0);pid_t child=fork();assert(child>=0);
 if(child==0){close(pipes[0]);retain_pipe=pipes[1];wrong_readback=true;std::freopen("/dev/null","w",stderr);Stop(nullptr,nullptr,false);(void)source_commit::Run(plan,*service);_exit(99);}
 close(pipes[1]);int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 char marker=0;assert(read(pipes[0],&marker,1)==0);close(pipes[0]);
 std::cout<<"Native source adapter: public calibration, named callees, wrong-thread pre-mutation refusal, heap roots restored before retain, field/monitor transfer and final native cache enumeration passed\n";
}

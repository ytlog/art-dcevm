#include "receiver_entry_metadata_fixture.inc"
#include "platform/common/native_receiver_entry_source.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <unordered_map>
namespace {
bool no_alloc=false,pending=false,wrong_flags=false,wrong_class=false,bounds_enabled=true,leave_native=false,decode_wrong=false,index_calibration=false,modern_inline=false;unsigned decodes=0,field_decodes=0,raises=0,root_reads=0;
void* actual_thread=nullptr;void*receiver_classes[2]{};int manager;void* method_objects[2]{};void* method_members[2]{};void* public_method_owner[2]{};void* public_field_owner[2]{};void* public_receivers[2]{};std::array<void*,2> public_fields{};
std::unordered_map<jobject,void*> handles;std::unordered_map<void*,jint> modifiers;size_t global_count=0;int modifier_id,declaring_id;
void* Resolve(jobject r){if(!r)return nullptr;auto i=handles.find(r);return i==handles.end()?reinterpret_cast<void*>(r):i->second;}
jobject Handle(void*object,uintptr_t kind){auto*slot=Allocate(8);Put32(slot,0,uint32_t(uintptr_t(object)));auto h=reinterpret_cast<jobject>(uintptr_t(slot)|kind);handles[h]=object;return h;}
void Jni(){assert(!no_alloc);}
jobjectRefType Kind(JNIEnv*,jobject r){Jni();if(!r)return JNIInvalidRefType;auto at=handles.find(r);if(at==handles.end())return JNIInvalidRefType;return (uintptr_t(r)&3)==2?JNIGlobalRefType:(uintptr_t(r)&3)==3?JNIWeakGlobalRefType:JNILocalRefType;}
jobject NewGlobal(JNIEnv*,jobject r){Jni();++global_count;return Handle(Resolve(r),2);}
void DeleteGlobal(JNIEnv*,jobject r){Jni();assert(handles.erase(r)==1&&global_count);--global_count;}
jobject NewLocal(JNIEnv*,jobject r){Jni();return Handle(Resolve(r),1);}
void DeleteLocal(JNIEnv*,jobject r){Jni();handles.erase(r);}
jclass Class(JNIEnv*,jobject r){Jni();auto*o=Resolve(r);if(o==method_objects[0]||o==method_objects[1]||o==public_fields[0]||o==public_fields[1])return reinterpret_cast<jclass>(bootstrap);for(size_t i=0;i<2;++i)if(o==public_receivers[i])return reinterpret_cast<jclass>(receiver_classes[i]);std::abort();}
jmethodID Method(JNIEnv*,jclass,const char*n,const char*){Jni();return reinterpret_cast<jmethodID>(!std::strcmp(n,"getModifiers")?&modifier_id:&declaring_id);}
jint Modifiers(JNIEnv*,jobject r,jmethodID,va_list){Jni();auto f=modifiers.at(Resolve(r));return wrong_flags?f^8:f;}
jobject Declaring(JNIEnv*,jobject r,jmethodID,va_list){Jni();auto*o=Resolve(r);for(size_t i=0;i<2;++i){if(o==method_objects[i])return reinterpret_cast<jobject>(wrong_class?public_method_owner[1-i]:public_method_owner[i]);if(o==public_fields[i])return reinterpret_cast<jobject>(wrong_class?public_field_owner[1-i]:public_field_owner[i]);}std::abort();}
jmethodID FromMethod(JNIEnv*,jobject r){Jni();for(size_t i=0;i<2;++i)if(Resolve(r)==method_objects[i])return reinterpret_cast<jmethodID>(method_members[i]);std::abort();}
jfieldID FromField(JNIEnv*,jobject r){Jni();if(index_calibration){for(size_t i=0;i<2;++i)if(Resolve(r)==public_fields[i])return reinterpret_cast<jfieldID>(uintptr_t(1+i*2));std::abort();}return reinterpret_cast<jfieldID>(static_cast<Mirror*>(Resolve(r))->field);}
void* Current(){return actual_thread;}
abi::arm64::android11::ReleaseObjectPointer Decode(const void*self,jobject r){assert(self==actual_thread);++decodes;return {uintptr_t(decode_wrong?public_receivers[1]:Resolve(r))};}
abi::arm64::android11::ReleaseObjectPointer DecodeGlobal(const void*self,jobject r){assert((uintptr_t(r)&3)==2);return Decode(self,r);}
void* EntryRoot(void*p){++root_reads;return Root(p);}
void* DecodeField(void*m,jfieldID id){assert(m==&manager);++field_decodes;if(uintptr_t(id)&1){assert(!no_alloc&&index_calibration);auto i=(uintptr_t(id)-1)/2;assert(i<2);return static_cast<Mirror*>(public_fields[i])->field;}return reinterpret_cast<void*>(id);}
void* DecodeMethod(void*m,jmethodID id){assert(m==&manager);return reinterpret_cast<void*>(id);}
void Throw(void*self,const char*,const char*){assert(self==actual_thread);++raises;}
void Suspend(void*,const char*,bool){}void Resume(void*){}
struct EntryHooks:Hooks{void*FindSymbol(const char*l,const char*n)override{
 if(!std::strcmp(n,abi::arm64::android11::kDecodeJObject))return modern_inline?nullptr:reinterpret_cast<void*>(Decode);
 if(!std::strcmp(n,abi::arm64::android14::kDecodeGlobalJObject))return reinterpret_cast<void*>(DecodeGlobal);
 if(!std::strcmp(n,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(EntryRoot);
 if(!std::strcmp(n,abi::common::kDecodeFieldId))return reinterpret_cast<void*>(DecodeField);
 if(!std::strcmp(n,abi::common::kDecodeMethodId))return reinterpret_cast<void*>(DecodeMethod);
 if(!std::strcmp(n,"_ZN3art6Thread17ThrowNewExceptionEPKcS2_"))return reinterpret_cast<void*>(Throw);
 if(!std::strcmp(n,abi::common::kSuspendConstruct))return reinterpret_cast<void*>(Suspend);
 if(!std::strcmp(n,abi::common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);
 return Hooks::FindSymbol(l,n);
}};
bool Extent(void*,const void*p,size_t*n){if(!bounds_enabled||!p)return false;auto a=uintptr_t(p),begin=uintptr_t(heap);if(a<begin||a>=begin+used)return false;*n=begin+used-a;return true;}
modern_field_metadata::ReadContext Bounds(void*,const runnable_jni::Context&){return {&manager,Extent};}
bool Strong(void*,const runnable_jni::Context&c,jobject r,void**out){assert(c.self==actual_thread);*out=Resolve(r);return *out!=nullptr;}
bool With(void*,JNIEnv*e,jobject,runnable_jni::Operation op,void*p){assert(e==&env);Put32(actual_thread,0,0);bool result=op({actual_thread,e,&vm},p);Put32(actual_thread,0,leave_native?0:92u<<24);return result;}
}
void*operator new(size_t n){if(no_alloc)std::abort();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(int argc,char**){
 modern_inline=argc>1;
 heap=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(heap!=MAP_FAILED&&uintptr_t(heap)+65536<=UINT32_MAX);
 JNINativeInterface_ table{};table.ExceptionCheck=[](JNIEnv*){Jni();return jboolean(pending);};env.functions=&table;
 EntryHooks hooks;std::string error;auto independent=Build(false,{true,false});bootstrap=independent.klass;
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));assert(metadata.Calibrate({&self,&env,&vm},reinterpret_cast<jobject>(bootstrap),"LBootstrap;",independent.fields.data(),independent.fields.size(),&error));
 actual_thread=Allocate(8);Put32(actual_thread,0,92u<<24);
 thread_suspend_service::Service state;assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&state));
 for(size_t i=0;i<2;++i){method_objects[i]=Allocate(8);method_members[i]=Allocate(32);Put32(method_members[i],0,uint32_t(uintptr_t(bootstrap)));Put32(method_members[i],4,i?9u:1u);public_method_owner[i]=bootstrap;public_field_owner[i]=bootstrap;modifiers[method_objects[i]]=i?9:1;public_fields[i]=independent.mirrors[1-i];modifiers[public_fields[i]]=i?9:1;}
 void*other_class=Allocate(160);public_receivers[0]=Allocate(16);public_receivers[1]=Allocate(16);Put32(public_receivers[0],0,uint32_t(uintptr_t(bootstrap)));Put32(public_receivers[1],0,uint32_t(uintptr_t(other_class)));receiver_classes[0]=bootstrap;receiver_classes[1]=other_class;
 table.GetJavaVM=[](JNIEnv*,JavaVM**out){Jni();*out=reinterpret_cast<JavaVM*>(&vm);return JNI_OK;};table.GetObjectRefType=Kind;table.NewGlobalRef=NewGlobal;table.DeleteGlobalRef=DeleteGlobal;table.NewLocalRef=NewLocal;table.DeleteLocalRef=DeleteLocal;table.GetObjectClass=Class;table.GetMethodID=Method;table.CallIntMethodV=Modifiers;table.CallObjectMethodV=Declaring;table.FromReflectedMethod=FromMethod;table.FromReflectedField=FromField;table.IsSameObject=[](JNIEnv*,jobject a,jobject b){Jni();return jboolean(Resolve(a)==Resolve(b));};
 receiver_guard::EntrySource::Samples samples;for(size_t i=0;i<2;++i){samples.methods[i]=env.NewGlobalRef(reinterpret_cast<jobject>(method_objects[i]));samples.fields[i]=env.NewGlobalRef(reinterpret_cast<jobject>(public_fields[i]));samples.receivers[i]=env.NewGlobalRef(reinterpret_cast<jobject>(public_receivers[i]));}samples.transition_reference=Handle(public_receivers[0],0);
 receiver_guard::EntrySource::Binding binding{&manager,reinterpret_cast<JavaVM*>(&vm),Current,Strong,With,Bounds,&manager};
 receiver_guard::EntrySource source;assert(source.Bind(hooks,binding,metadata,state,&error)&&!source.Proven());auto access=source.Leaves();assert(!access.current(access.owner,actual_thread));
 const auto baseline=global_count;wrong_flags=true;assert(!source.Calibrate(&env,samples,&error)&&!source.Proven()&&global_count==baseline);wrong_flags=false;
 decode_wrong=true;assert(!source.Calibrate(&env,samples,&error)&&!source.Proven()&&global_count==baseline);decode_wrong=false;
 Put32(public_receivers[0],0,uint32_t(uintptr_t(other_class)));assert(!source.Calibrate(&env,samples,&error)&&!source.Proven()&&global_count==baseline);Put32(public_receivers[0],0,uint32_t(uintptr_t(bootstrap)));
 bounds_enabled=false;assert(!source.Calibrate(&env,samples,&error)&&global_count==baseline);bounds_enabled=true;
 assert(source.Calibrate(&env,samples,&error)&&source.Proven()&&source.TransitionProven()&&global_count==baseline);assert(!source.Calibrate(&env,samples,&error));
 runnable_jni::Context context{actual_thread,&env,&vm};void*out=nullptr;uint32_t flags=0;
 // Native state and an OS-style extent alone cannot grant a scope.
 {receiver_guard::EntrySource::InvocationScope scope(source,context,Bounds(nullptr,context));assert(!scope.Valid()&&!access.current(access.owner,actual_thread));}
 Put32(actual_thread,0,0);auto local=env.NewLocalRef(samples.receivers[0]);auto weak=Handle(public_receivers[0],3);auto*field=independent.native[1];
 no_alloc=true;
 {receiver_guard::EntrySource::InvocationScope scope(source,context,Bounds(nullptr,context));assert(scope.Valid()&&access.current(access.owner,actual_thread));
  {receiver_guard::EntrySource::InvocationScope nested(source,context,Bounds(nullptr,context));assert(!nested.Valid());}
  for(size_t i=0;i<2;++i){assert(access.member(access.owner,method_members[i],false,&flags,&out)&&out==bootstrap&&bool(flags&8)==bool(i));assert(access.member(access.owner,independent.native[1-i],true,&flags,&out)&&out==bootstrap&&bool(flags&8)==bool(i));assert(access.object_class(access.owner,public_receivers[i],&out)&&out==(i?other_class:bootstrap));assert(access.reference(access.owner,actual_thread,samples.receivers[i],&out)&&out==public_receivers[i]);assert(access.compressed(access.owner,actual_thread,uint32_t(uintptr_t(public_receivers[i])),&out)&&out==public_receivers[i]);}
  assert(access.reference(access.owner,actual_thread,local,&out)&&out==public_receivers[0]);assert(access.reference(access.owner,actual_thread,samples.transition_reference,&out)&&out==public_receivers[0]);
  if(modern_inline){
   auto before_roots=root_reads,before_decode=decodes;
   auto*slot=reinterpret_cast<void*>(uintptr_t(local)&~uintptr_t(3));
   Put32(slot,0,1);assert(!access.reference(access.owner,actual_thread,local,&out)&&root_reads==before_roots&&decodes==before_decode);
   Put32(slot,0,0);assert(!access.reference(access.owner,actual_thread,local,&out)&&root_reads==before_roots&&decodes==before_decode);
   Put32(slot,0,uint32_t(uintptr_t(public_receivers[0])));
   assert(!access.reference(access.owner,actual_thread,reinterpret_cast<void*>(0x1001),&out)&&root_reads==before_roots&&decodes==before_decode);
  }
  auto old_decode=decodes;assert(!access.reference(access.owner,actual_thread,weak,&out)&&decodes==old_decode);
  auto old_field=field_decodes;assert(!access.field_id(access.owner,actual_thread,reinterpret_cast<void*>(1),&out)&&field_decodes==old_field);
  assert(access.field_id(access.owner,actual_thread,field,&out)&&out==field&&field_decodes==old_field+1);
  assert(access.reference(access.owner,actual_thread,nullptr,&out)&&out==nullptr);assert(access.compressed(access.owner,actual_thread,0,&out)&&out==nullptr);
  bounds_enabled=false;old_decode=decodes;assert(!access.member(access.owner,method_members[0],false,&flags,&out)&&!access.reference(access.owner,actual_thread,local,&out)&&decodes==old_decode);bounds_enabled=true;
  Put32(actual_thread,0,92u<<24);assert(!access.current(access.owner,actual_thread));Put32(actual_thread,0,0);
  auto*saved=actual_thread;actual_thread=other_class;assert(!access.current(access.owner,saved));actual_thread=saved;
  assert(!access.member(access.owner,reinterpret_cast<void*>(1),false,&flags,&out));assert(!access.object_class(access.owner,reinterpret_cast<void*>(1),&out));
  access.raise(access.owner,actual_thread,"Ljava/lang/IncompatibleClassChangeError;","fixture");assert(raises==1);
 }
 assert(!access.current(access.owner,actual_thread));no_alloc=false;
 // Omitted actual transition calibration does not silently decode kind zero.
 receiver_guard::EntrySource no_transition;assert(no_transition.Bind(hooks,binding,metadata,state,&error));Put32(actual_thread,0,92u<<24);auto no_transition_samples=samples;no_transition_samples.transition_reference=nullptr;assert(no_transition.Calibrate(&env,no_transition_samples,&error)&&!no_transition.TransitionProven());Put32(actual_thread,0,0);
 {receiver_guard::EntrySource::InvocationScope scope(no_transition,context,Bounds(nullptr,context));assert(scope.Valid());auto leaf=no_transition.Leaves();auto before=decodes;assert(!leaf.reference(leaf.owner,actual_thread,samples.transition_reference,&out)&&before==decodes);}
 // Indexed public IDs can calibrate native metadata ordinarily; permanent
 // no-wait dispatch still refuses the same index before the lock-taking leaf.
 receiver_guard::EntrySource indexed;assert(indexed.Bind(hooks,binding,metadata,state,&error));index_calibration=true;Put32(actual_thread,0,92u<<24);assert(indexed.Calibrate(&env,samples,&error));Put32(actual_thread,0,0);
 {receiver_guard::EntrySource::InvocationScope scope(indexed,context,Bounds(nullptr,context));assert(scope.Valid());auto leaf=indexed.Leaves();auto before=field_decodes;no_alloc=true;assert(!leaf.field_id(leaf.owner,actual_thread,reinterpret_cast<void*>(1),&out)&&before==field_decodes);no_alloc=false;}index_calibration=false;
#ifdef ARTDCEVM_ORIGINAL_INGRESS_TEST
 RunIngressTest(source,context,method_members[0],samples.receivers[0],bootstrap);
#endif
 env.DeleteLocalRef(local);for(size_t i=0;i<2;++i){env.DeleteGlobalRef(samples.methods[i]);env.DeleteGlobalRef(samples.fields[i]);env.DeleteGlobalRef(samples.receivers[i]);}assert(global_count==0);
 std::puts("Actual independently calibrated FieldMetadata plus public reflected Method/Field/Object/strong-local-global-transition JNI oracle: permanent source leaves, exact invocation/state/root scope, weak/indices refusal before decoder, no allocation/JNI/OS during reads and temporary-root balance PASS; full Guard proof/device not claimed");
}

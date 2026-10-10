#include "platform/common/contextual_field_id.cc"
#include "platform/common/native_field_factory_observer.cc"
#include "platform/common/modern_retirement_cleanup.cc"
#include <cassert>
#include <cstdarg>
#include <iostream>
#include <future>
#include <chrono>
#include <map>
#include <set>
using namespace artdcevm;
namespace {
struct Field {jclass owner;jfieldID id;void* native;bool stat;const char* name;};
jclass Class(size_t n){return reinterpret_cast<jclass>(0x1000+n*16);}
Field fields[]={{Class(1),reinterpret_cast<jfieldID>(0x40002000),reinterpret_cast<void*>(0x7000001000),false,"first"},{Class(1),reinterpret_cast<jfieldID>(17),reinterpret_cast<void*>(0x7000002000),true,"second"},{Class(2),reinterpret_cast<jfieldID>(0x40002000),reinterpret_cast<void*>(0x7000003000),false,"first"}};
std::set<jobject> cleared;size_t weak_created=0,weak_deleted=0,strong_created=0,strong_deleted=0;bool pending=false,wrong_roundtrip=false,wrong_native=false,weak_throws=false,global_throws=false;std::string fail_method;int fail_call=0,call_index=0;
std::map<std::string,jmethodID> methods;size_t next_method=1;
Field& AsField(jobject f){return *reinterpret_cast<Field*>(f);}
bool IsField(jobject f){return f==reinterpret_cast<jobject>(&fields[0])||f==reinterpret_cast<jobject>(&fields[1])||f==reinterpret_cast<jobject>(&fields[2]);}
jboolean IsInstance(JNIEnv*,jobject object,jclass){return IsField(object);}
jboolean Exception(JNIEnv*){return pending;}
jclass Find(JNIEnv*,const char*){assert(!pending);return Class(10);}
jclass ObjectClass(JNIEnv*,jobject object){assert(!pending);return IsField(object)?Class(11):reinterpret_cast<jclass>(object);}
jmethodID Method(JNIEnv*,jclass owner,const char* name,const char*){assert(!pending);if(fail_method==name||(fail_method=="Class.getName"&&owner==Class(10)&&std::string(name)=="getName")){pending=true;return nullptr;}auto it=methods.find(name);if(it!=methods.end())return it->second;auto id=reinterpret_cast<jmethodID>(next_method++);methods[name]=id;return id;}
std::string Name(jmethodID id){for(auto& p:methods)if(p.second==id)return p.first;assert(false);return {};}
jobject CallObject(JNIEnv*,jobject object,jmethodID id,va_list){assert(!pending);if(fail_call&&++call_index==fail_call){pending=true;return nullptr;}auto name=Name(id);if(name=="getDeclaringClass")return AsField(object).owner;if(name=="getType")return Class(12);if(name=="getName")return reinterpret_cast<jobject>(const_cast<char*>(IsField(object)?AsField(object).name:"int"));assert(false);return nullptr;}
jint CallInt(JNIEnv*,jobject object,jmethodID id,va_list){assert(!pending);if(fail_call&&++call_index==fail_call){pending=true;return 0;}assert(Name(id)=="getModifiers");return AsField(object).stat?8:0;}
jboolean CallBool(JNIEnv*,jobject object,jmethodID id,va_list args){assert(!pending);if(fail_call&&++call_index==fail_call){pending=true;return false;}assert(Name(id)=="equals");return object==va_arg(args,jobject);}
const char* Utf(JNIEnv*,jstring string,jboolean*){assert(!pending);return reinterpret_cast<const char*>(string);}
void ReleaseUtf(JNIEnv*,jstring,const char*){}
jfieldID FieldId(JNIEnv*,jclass owner,const char* name,const char* sig){assert(std::string(sig)=="I");for(auto& f:fields)if(f.owner==owner&&std::string(f.name)==name&&!f.stat)return f.id;pending=true;return nullptr;}
jfieldID StaticFieldId(JNIEnv*,jclass owner,const char* name,const char* sig){assert(std::string(sig)=="I");for(auto& f:fields)if(f.owner==owner&&std::string(f.name)==name&&f.stat)return f.id;pending=true;return nullptr;}
jfieldID From(JNIEnv*,jobject field){assert(!pending);return AsField(field).id;}
jobject To(JNIEnv*,jclass owner,jfieldID id,jboolean stat){assert(!pending);for(auto& f:fields)if(f.owner==owner&&f.id==id&&f.stat==bool(stat))return reinterpret_cast<jobject>(wrong_roundtrip?&fields[2]:&f);pending=true;return nullptr;}
jobject NewStrong(JNIEnv*,jobject object){assert(!pending);++strong_created;if(global_throws)pending=true;return object;}
void DeleteStrong(JNIEnv*,jobject){++strong_deleted;}
jweak NewWeak(JNIEnv*,jobject object){assert(!pending);++weak_created;if(weak_throws)pending=true;return object;}
void DeleteWeak(JNIEnv*,jweak){++weak_deleted;}
jobject NewLocal(JNIEnv*,jobject object){return cleared.count(object)?nullptr:object;}
void DeleteLocal(JNIEnv*,jobject){}
jboolean Same(JNIEnv*,jobject a,jobject b){if(cleared.count(a))a=nullptr;if(cleared.count(b))b=nullptr;return a==b;}
jboolean Assignable(JNIEnv*,jclass a,jclass b){return a==b;}
void* Leaf(void* mirror){return wrong_native?reinterpret_cast<void*>(0x9000000000):reinterpret_cast<Field*>(mirror)->native;}
namespace factory=native_field_factory_observer;
int factory_thread,factory_owner;void* factory_members[2]{};size_t factory_calls=0;factory::Factory factory_proxy=nullptr;
void* FactoryCurrent(){return &factory_thread;}
bool FactoryRoot(void*,void*,const void*,void**){return false;}
factory::Return ActualFactory(void*self,factory::HandleCarrier,void*field,bool force){
 assert(self==&factory_thread&&!force&&!pending);++factory_calls;
 // Fixed source FindFieldIndex requires physical membership in current Class.
 if(field!=factory_members[0]&&field!=factory_members[1]){pending=true;return {0};}
 return {reinterpret_cast<uintptr_t>(field)};
}
bool AliasNormalize(void*,void*self,factory::HandleCarrier,void*input,bool,void**out)noexcept{
 assert(self==&factory_thread);auto*target=contextual_field_id::NativeSurvivingTarget(input);*out=target?target:input;return true;
}
struct FactoryHooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char*name)override{return std::strstr(name,"CreateFromArtField")?reinterpret_cast<void*>(ActualFactory):nullptr;}
 void* Install(void*,void*proxy,void**original,std::string*)override{factory_proxy=reinterpret_cast<factory::Factory>(proxy);*original=reinterpret_cast<void*>(ActualFactory);return this;}
 bool Remove(void*token,std::string*)override{return token==this;}
};
struct Hooks:HookEngine {bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* symbol)override{assert(std::string(symbol)=="_ZN3art6mirror5Field11GetArtFieldEv");return reinterpret_cast<void*>(Leaf);}void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
}
namespace artdcevm::runnable_executor {
bool Available(){return true;}
bool reject_decode=false;bool reject_reentry=false;
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject root,void** out){*out=reinterpret_cast<void*>(root);return root&&!reject_decode;}
runnable_jni::Result With(JNIEnv* env,jobject,runnable_jni::Operation operation,void* data){if(reject_reentry)return runnable_jni::Result::kReentrant;return operation({reinterpret_cast<void*>(1),env,nullptr},data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}

namespace artdcevm::method_identity {bool ReadableMappingExtent(const void*,size_t*){return false;}}
namespace {
std::shared_ptr<contextual_field_id::Proof> gc_proof;size_t gc_forwarded=0,gc_expected_weak_deletes=0;
int gc_linker,gc_thread;
bool Arena(const void*,const void*){return true;}
void OriginalCleanup(void* linker,void* thread,const void*,bool cha){assert(linker==&gc_linker&&thread==&gc_thread&&cha);assert(gc_proof->invalid.load()&&weak_deleted==gc_expected_weak_deletes);++gc_forwarded;}
}
int main(){JNINativeInterface_ table{};table.ExceptionCheck=Exception;table.IsInstanceOf=IsInstance;table.FindClass=Find;table.GetObjectClass=ObjectClass;table.GetMethodID=Method;table.CallObjectMethodV=CallObject;table.CallIntMethodV=CallInt;table.CallBooleanMethodV=CallBool;table.GetStringUTFChars=Utf;table.ReleaseStringUTFChars=ReleaseUtf;table.GetFieldID=FieldId;table.GetStaticFieldID=StaticFieldId;table.FromReflectedField=From;table.ToReflectedField=To;table.NewGlobalRef=NewStrong;table.DeleteGlobalRef=DeleteStrong;table.NewWeakGlobalRef=NewWeak;table.DeleteWeakGlobalRef=DeleteWeak;table.NewLocalRef=NewLocal;table.DeleteLocalRef=DeleteLocal;table.IsSameObject=Same;table.IsAssignableFrom=Assignable;JNIEnv env{&table};Hooks hooks;std::string error;using namespace contextual_field_id;
 assert(Initialize(hooks,&error));auto f=reinterpret_cast<jobject>(&fields[0]),s=reinterpret_cast<jobject>(&fields[1]);
 for(const char* name:{"getDeclaringClass","getName","getType","getModifiers","equals","Class.getName"}){fail_method=name;assert(!Prepare(&env,f,fields[0].id,&error)&&pending);pending=false;}fail_method.clear();
 for(int failure=1;failure<=6;failure++){fail_call=failure;call_index=0;assert(!Prepare(&env,f,fields[0].id,&error)&&pending);pending=false;}fail_call=0;
 weak_throws=true;assert(!Prepare(&env,f,fields[0].id,&error)&&pending);pending=false;weak_throws=false;assert(weak_created==weak_deleted);
 global_throws=true;assert(!Prepare(&env,f,fields[0].id,&error)&&pending);pending=false;global_throws=false;assert(strong_created==strong_deleted);
 auto first=Prepare(&env,f,fields[0].id,&error),second=Prepare(&env,s,fields[1].id,&error);assert(first&&second&&NativeField(first)==fields[0].native&&!IsStatic(first)&&IsStatic(second));
 assert(DecodeLive(&env,Class(1),false,fields[0].id,&error)==fields[0].native);
 runnable_executor::reject_decode=true;assert(!DecodeLive(&env,Class(1),false,fields[0].id,&error)&&error.find("strong-Field-root-decode-rejected")!=std::string::npos);runnable_executor::reject_decode=false;
 runnable_executor::reject_reentry=true;assert(!DecodeLive(&env,Class(1),false,fields[0].id,&error)&&error.find("reentrant Runnable executor")!=std::string::npos&&error.find("callback-not-entered")!=std::string::npos);runnable_executor::reject_reentry=false;
 assert(HasNativeProof(fields[1].native)&&!HasNativeProof(fields[0].native)&&!HasNativeProof(reinterpret_cast<void*>(0x9900)));
 retirement::RetiredFieldRegistry survivor_registry;
 auto* target_a=reinterpret_cast<void*>(0x700000a000);auto* target_b=reinterpret_cast<void*>(0x700000b000);
 {std::unique_lock<std::mutex> held(aliases_mutex);auto native=std::async(std::launch::async,[&]{PublishNativeSurviving(fields[1].native,target_a,survivor_registry);assert(NativeSurvivingTarget(fields[1].native)==target_a);});
  assert(native.wait_for(std::chrono::seconds(1))==std::future_status::ready);native.get();}
 // This exact public opaque-ID proof avoids reconstructing a mirror from an
 // alias address outside the replacement Class's physical field array.
 wrong_roundtrip=true;assert(DecodeLive(&env,Class(1),true,fields[1].id,&error)==target_a);wrong_roundtrip=false;
 factory_members[0]=target_a;factory_members[1]=target_b;
 assert(!ActualFactory(&factory_thread,{nullptr},fields[1].native,false).reference&&pending);pending=false;
 FactoryHooks factory_hooks;factory::Service factory_service;void* factory_roots[]{&factory_owner};
 exclusive_owned_roots::Context factory_context{&factory_thread,factory_roots,1};
 assert(factory_service.Bind(factory_hooks,{&factory_owner,FactoryCurrent,FactoryRoot},&error));
 assert(factory_service.SetNormalizerAtExclusive({&factory_owner,AliasNormalize},factory_context,&error));
 assert(factory_service.InstallAtExclusive(factory_hooks,factory_context,&error)&&factory_service.NormalizerInstalled());
 assert(factory_proxy(&factory_thread,{nullptr},fields[1].native,false).reference==reinterpret_cast<uintptr_t>(target_a)&&!pending);
 PublishNativeSurviving(fields[1].native,target_b,survivor_registry);assert(DecodeLive(&env,Class(1),true,fields[1].id,&error)==target_b);
 assert(factory_proxy(&factory_thread,{nullptr},fields[1].native,false).reference==reinterpret_cast<uintptr_t>(target_b)&&!pending);
 assert(factory_service.RemoveAtExclusive(factory_hooks,factory_context,&error));
 // Apply's ordinary scope release must retain the independently captured weak
 // namespace and mapping for the cached ID used after that Apply returns.
 auto scoped_alias=second;const auto deletes_before_release=weak_deleted;
 Release(&env,&scoped_alias);assert(!scoped_alias&&weak_deleted==deletes_before_release);
 assert(NativeSurvivingTarget(fields[1].native)==target_b);
 assert(!DecodeLive(&env,Class(2),true,fields[1].id,&error)&&pending);pending=false;
 pending=true;assert(!DecodeLive(&env,Class(1),true,fields[1].id,&error)&&pending);pending=false;

 assert(!Prepare(&env,f,fields[1].id,&error));wrong_roundtrip=true;assert(!Prepare(&env,f,fields[0].id,&error));wrong_roundtrip=false;
 auto& registry=survivor_registry;retirement::RetiredFieldRegistry wrong_registry;registry.Retire(fields[0].native);PublishRetired(first,registry);assert(Retired(&env,Class(1),false,fields[0].id,registry));
 assert(!Prepare(&env,f,fields[0].id,&error));
 assert(!Retired(&env,Class(2),false,fields[0].id,registry)&&!Retired(&env,Class(1),true,fields[0].id,registry)&&!Retired(&env,Class(1),false,fields[0].id,wrong_registry));
 // A new live field reusing the opaque ID must be refused, never hidden by alias.
 auto old=fields[0].native;fields[0].native=reinterpret_cast<void*>(0x7000009000);assert(!Prepare(&env,f,fields[0].id,&error)&&error.find("collides")!=std::string::npos);fields[0].native=old;
 // Same pointer and recycled mirror tag still represent a new native epoch.
 auto previous=registry.IdentityFor(old);registry.Prune(nullptr,[](const void*,void*){return true;});registry.Retire(old);
 auto current=registry.IdentityFor(old);assert(previous.token==current.token&&previous.generation!=current.generation);
 assert(!Retired(&env,Class(1),false,fields[0].id,registry));
 // A paused ordinary JNI reader may hold its lock throughout exclusive native
 // publication/notification. Those paths must not acquire or wait for it.
 registry.Retire(fields[1].native);
 {std::unique_lock<std::mutex> held(aliases_mutex);auto native=std::async(std::launch::async,[&]{PublishNativeRetired(fields[1].native,registry);NativeNotifyPrune(registry);});
  assert(native.wait_for(std::chrono::seconds(1))==std::future_status::ready);native.get();}
 assert(first->invalid.load()&&second->published.load()&&!NativeSurvivingTarget(fields[1].native)&&!HasNativeProof(fields[1].native));
 // Ordinary sweep defers node reclamation while native snapshot readers exist.
 native_readers.fetch_add(1);Prune(&env,registry);assert(!deferred_slots.empty());native_readers.fetch_sub(1);
 Prune(&env,registry);assert(deferred_slots.empty());
 Release(&env,&second);assert(weak_created-weak_deleted==1&&strong_created==strong_deleted);
 cleared.insert(Class(1));Prune(&env,registry);assert(!Retired(&env,Class(1),false,fields[0].id,registry));Release(&env,&first);assert(weak_created==weak_deleted&&strong_created==strong_deleted);
 // Published temporary owner destruction must not leave a dangling registry read;
 // placement reuse at the same wrapper address is a distinct lifetime cookie.
 auto other_field=reinterpret_cast<jobject>(&fields[2]);auto temporary_proof=Prepare(&env,other_field,fields[2].id,&error);assert(temporary_proof);
 alignas(retirement::RetiredFieldRegistry) unsigned char arena[sizeof(retirement::RetiredFieldRegistry)];
 auto* temporary=new(arena)retirement::RetiredFieldRegistry;temporary->Retire(fields[2].native);PublishRetired(temporary_proof,*temporary);
 auto old_cookie=temporary->LifetimeState();temporary->~RetiredFieldRegistry();assert(!old_cookie->Alive());
 auto* replacement=new(arena)retirement::RetiredFieldRegistry;replacement->Retire(fields[2].native);
 assert(replacement->LifetimeState()!=old_cookie&&!Retired(&env,Class(2),false,fields[2].id,*replacement));
 auto valid_new=Prepare(&env,other_field,fields[2].id,&error);assert(valid_new);Release(&env,&valid_new);
 NativeNotifyPrune(*replacement);Prune(&env,*replacement);Release(&env,&temporary_proof);replacement->~RetiredFieldRegistry();
 assert(weak_created==weak_deleted);
 // Actual production loader cleanup TU prunes, notifies alias epochs after the
 // registry lock exits, and forwards original arguments without any JNI call.
 gc_proof=Prepare(&env,other_field,fields[2].id,&error);assert(gc_proof);registry.Retire(fields[2].native);PublishRetired(gc_proof,registry);
 retirement::RetiredMethodRegistry method_registry;method_registry.Retire(reinterpret_cast<void*>(0x70004000));
 retirement::fields=&registry;retirement::methods=&method_registry;retirement::arena_contains=Arena;
 retirement::original=OriginalCleanup;retirement::declaration=class_loader_cleanup::Declaration::kPrepareForBatchDelete;
 class_loader_abi::arm64::ClassLoaderData loader{nullptr,nullptr,reinterpret_cast<void*>(0x80000000)};
 gc_expected_weak_deletes=weak_deleted;
 {std::unique_lock<std::mutex> held(aliases_mutex);retirement::Cleanup(&gc_linker,&gc_thread,&loader,true);}
 assert(gc_forwarded==1&&registry.Count()==0&&method_registry.Count()==0&&gc_proof->invalid.load()&&weak_deleted==gc_expected_weak_deletes);
 Prune(&env,registry);Release(&env,&gc_proof);assert(weak_created==weak_deleted);
 // Live alias weak owner unload and registry lifetime expiry cannot leave a
 // cached native target usable by a reused owner or a later operation.
 cleared.erase(Class(1));auto live_unload=Prepare(&env,s,fields[1].id,&error);assert(live_unload);
 PublishNativeSurviving(fields[1].native,target_a,registry);Release(&env,&live_unload);
 cleared.insert(Class(1));Prune(&env,registry);assert(!NativeSurvivingTarget(fields[1].native)&&!HasNativeProof(fields[1].native));
 cleared.erase(Class(1));auto live_expiry=Prepare(&env,s,fields[1].id,&error);assert(live_expiry);
 {
  retirement::RetiredFieldRegistry ephemeral;
  PublishNativeSurviving(fields[1].native,target_a,ephemeral);Release(&env,&live_expiry);
  assert(NativeSurvivingTarget(fields[1].native)==target_a);
 }
 assert(!NativeSurvivingTarget(fields[1].native)&&!HasNativeProof(fields[1].native));Prune(&env,registry);
 assert(weak_created==weak_deleted);
 pending=true;assert(!Prepare(&env,f,fields[0].id,&error)&&!DecodeLive(&env,Class(1),false,fields[0].id,&error)&&pending);pending=false;
 std::cout<<"Contextual JNI field identity: public even/odd roundtrip, scoped leaf proof, class/static/registry namespace, opaque collision refusal, weak cleanup, same-pointer/tag epochs, owner destruction/reuse, lock-free native publication and pending preservation passed\n";
}

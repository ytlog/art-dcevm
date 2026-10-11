#pragma once
// Independent ART/JNI leaf protocol for the real production owners.
// No private permission, inventory, selection or Session state is forged.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_call_graph_leaf_fixture.h"

#pragma clang diagnostic pop
#include "platform/common/native_call_graph_stack.h"
#include "platform/common/runtime_instance_probe.cc"
#include "platform/common/runtime_service_calibration.cc"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_call_graph_runtime_fixture.h"
namespace artdcevm::runnable_executor {
// This host leaf is the independently configured public JNI callback bridge.
bool Initialize(HookEngine&,JNIEnv*,std::string*) { return true; }
}
#pragma clang diagnostic pop
namespace drain_fixture {

void* root_carrier=nullptr;
jobjectArray RootArray(JNIEnv*,jsize count,jclass,jobject){Jni();auto*p=heap.New(12+size_t(count)*4);Put32(p,8,uint32_t(count));root_carrier=p;return reinterpret_cast<jobjectArray>(p);}
void RootSet(JNIEnv*,jobjectArray a,jsize i,jobject value){Jni();assert(i>=0&&uint32_t(i)<Read32(a,8));Put32(a,12+size_t(i)*4,uint32_t(uintptr_t(value)));}
jobject RootGet(JNIEnv*,jobjectArray a,jsize i){Jni();assert(i>=0&&uint32_t(i)<Read32(a,8));return reinterpret_cast<jobject>(uintptr_t(Read32(a,12+size_t(i)*4)));}
jsize RootLength(JNIEnv*,jarray a){Jni();return jsize(Read32(a,8));}
bool RootScope(void*,JNIEnv*e,jobject,runnable_jni::Operation operation,void*p){assert(!stopped&&Read32(thread.data())==0);return operation({thread.data(),e,&::runtime},p);}
bool RootDecode(void*,jobject p,void**out){*out=p;return bool(p);}
bool RootOracle(void*,const runnable_jni::Context&,jobject p,void**out){return RootDecode(nullptr,p,out);}
void*RootElement(void*,void*a,size_t i){assert(i<Read32(a,8));return reinterpret_cast<void*>(uintptr_t(Read32(a,12+i*4)));}
namespace s=abi::arm64::android_common::source_stop;
int gc_heap,mutator_mutex;void*mutator=&mutator_mutex;bool gc=false;size_t reset_calls=0,walk_calls=0;
using Op=s::MutatorOperation;Op disable_proxy=nullptr,enable_proxy=nullptr;
abi::common::SuspendConstruct stop_proxy=nullptr;abi::common::SuspendDestroy resume_proxy=nullptr;
s::StartGC start_proxy=nullptr;s::FinishGC finish_proxy=nullptr;
void Native(void*self){assert(self==thread.data()&&!stopped&&Read32(self)==0);Put32(self,0,92u<<24);}
void Runnable(void*self){assert(self==thread.data()&&!stopped&&Read32(self)==92u<<24);Put32(self,0,0);}
void Disable(void*p,void*t){assert(p==&gc_heap&&t==thread.data()&&!stopped);}
void Enable(void*p,void*t){assert(p==&gc_heap&&t==thread.data()&&!stopped);}
void Start(void*p,void*t,int32_t cause,int32_t collector){assert(p==&gc_heap&&t==thread.data()&&!gc&&!stopped&&cause==8&&collector==1);gc=true;}
void Finish(void*p,void*t,int32_t type){assert(p==&gc_heap&&t==thread.data()&&gc&&!stopped&&!type);gc=false;}
void Stop(void*,const char*,bool){assert(!stopped&&Read32(thread.data())==92u<<24);stopped=1;}
void Resume(void*){assert(stopped==1);stopped=0;}
void Lock(void*p,void*t){assert(p==mutator&&t==thread.data()&&gc&&!stopped);stopped=1;}
void Unlock(void*p,void*t){assert(p==mutator&&t==thread.data()&&gc&&stopped);stopped=0;}
void ListLock(void*p,void*t){assert(p==&mutex&&t==thread.data()&&stopped&&!locked);locked=1;}
void ListUnlock(void*p,void*t){assert(p==&mutex&&t==thread.data()&&stopped&&locked);locked=0;}
void*list_lock=&mutex;int third_thread;void*registered[]{thread.data(),foreign_thread.data(),&third_thread};
void ForEach(void*receiver,void(*callback)(void*,void*),void*state){assert(receiver==&thread_list&&stopped&&locked);for(void*t:registered)callback(t,state);}
struct Closure:art::Closure{void Run(art::Thread*)override{std::abort();}}closure;
void PrepareCache(){assert(!stopped&&Read32(thread.data())==92u<<24);}
struct Context final:abi::arm64::context::Prefix{
 void Reset()override{assert(stopped&&locked);++reset_calls;}void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{std::abort();}
 void SetSP(uintptr_t)override{std::abort();}void SetPC(uintptr_t)override{std::abort();}void SetArg0(uintptr_t)override{std::abort();}
 bool IsAccessibleGPR(uint32_t)override{return false;}uintptr_t*GetGPRAddress(uint32_t)override{return nullptr;}uintptr_t GetGPR(uint32_t)override{return 0;}void SetGPR(uint32_t,uintptr_t)override{std::abort();}
 bool IsAccessibleFPR(uint32_t)override{return false;}uintptr_t GetFPR(uint32_t)override{return 0;}
};
abi::arm64::context::Prefix*Create(){assert(!forbid_provider_allocation);return new Context;}
bool boundary_geometry=false;
struct Frame{void*method=nullptr;bool shadow=false;alignas(16) uintptr_t words[8]{};uintptr_t*carrier=nullptr;uintptr_t pc=0;};std::vector<Frame>frames[3];void*current_method=nullptr;
void Construct(void*raw,void*target,void*context,stack_visitor_service::WalkKind kind,bool checked){assert(stopped&&locked&&context&&kind==stack_visitor_service::WalkKind::kIncludeInlined&&checked);auto*v=static_cast<abi::arm64::stack::Visitor13*>(raw);v->thread=target;v->kind=kind;v->context=context;v->shadow_frame=nullptr;v->quick_frame=nullptr;}
void Walk(void*raw,bool transitions){assert(stopped&&locked&&transitions);++walk_calls;auto*v=static_cast<abi::arm64::stack::Visitor13*>(raw);size_t n=0;while(n<3&&registered[n]!=v->thread)++n;assert(n<3);size_t depth=0;for(const auto&f:frames[n]){v->depth=depth++;v->pc=f.pc;current_method=f.method;v->shadow_frame=f.shadow?const_cast<Frame*>(&f):nullptr;auto*words=f.carrier?f.carrier:const_cast<uintptr_t*>(f.words);words[0]=uintptr_t(f.method);words[7]=0x123450;
 v->quick_frame=f.method&&!f.shadow?(boundary_geometry?reinterpret_cast<void**>(words):&current_method):nullptr;if(!v->VisitFrame())break;}}
void*Method(const void*){assert(stopped&&locked);return current_method;}
std::vector<void*>deoptimized;size_t queries=0,deopt_calls=0,undeopt_calls=0;
size_t fail_deopt=SIZE_MAX,fail_undeopt=SIZE_MAX;
void Deopt(void*owner,void*method){assert(owner==early_owner_fixture::instrumentation&&stopped);if(deopt_calls++==fail_deopt)return;deoptimized.push_back(method);}
void Undeopt(void*owner,void*method){assert(owner==early_owner_fixture::instrumentation&&stopped);auto found=std::find(deoptimized.begin(),deoptimized.end(),method);assert(found!=deoptimized.end());if(undeopt_calls++==fail_undeopt)return;deoptimized.erase(found);}
bool Query(void*owner,void*method){assert(owner==early_owner_fixture::instrumentation&&stopped);++queries;return std::find(deoptimized.begin(),deoptimized.end(),method)!=deoptimized.end();}
native_runtime_frame::FrameInfo FrameInfo(const void*){assert(stopped&&locked);return {64,0x40000000u,0};}
uintptr_t return_address_offset=56;
uintptr_t ReturnAddress(const void*raw){assert(stopped&&locked);const auto*v=static_cast<const abi::arm64::stack::Visitor13*>(raw);return uintptr_t(v->quick_frame)+return_address_offset;}
struct Hooks:HookEngine{
 RootHooks delegate;bool Initialize(std::string*)override{return true;}bool Remove(void*,std::string*)override{return false;}
 void*FindSymbol(const char*lib,const char*n)override{
  if(boundary_geometry&&!std::strcmp(n,native_runtime_frame::Service::kFrameInfo))return reinterpret_cast<void*>(FrameInfo);
  if(boundary_geometry&&!std::strcmp(n,native_runtime_frame::Service::kReturnAddress))return reinterpret_cast<void*>(ReturnAddress);
  using SS=stack_visitor_service::Service;
  if(!std::strcmp(n,SS::ConstructSymbol()))return reinterpret_cast<void*>(Construct);
  if(!std::strcmp(n,SS::WalkSymbol(stack_visitor_service::Declaration::kAndroid13To16)))return reinterpret_cast<void*>(Walk);
  if(!std::strcmp(n,SS::MethodSymbol()))return reinterpret_cast<void*>(Method);
  if(!std::strcmp(n,abi::arm64::context::kCreate))return reinterpret_cast<void*>(Create);
  if(!std::strcmp(n,abi::arm64::android13::kToNative))return reinterpret_cast<void*>(Native);
  if(!std::strcmp(n,abi::arm64::android13::kToRunnable))return reinterpret_cast<void*>(Runnable);
  if(!std::strcmp(n,s::kMutator))return &mutator;if(!std::strcmp(n,s::kCurrent))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(n,s::kStartGC))return reinterpret_cast<void*>(start_proxy?start_proxy:Start);
  if(!std::strcmp(n,s::kFinishGC))return reinterpret_cast<void*>(finish_proxy?finish_proxy:Finish);
  if(!std::strcmp(n,s::kDisableMoving))return reinterpret_cast<void*>(disable_proxy?disable_proxy:Disable);
  if(!std::strcmp(n,s::kEnableMoving))return reinterpret_cast<void*>(enable_proxy?enable_proxy:Enable);
  if(!std::strcmp(n,s::kLock))return reinterpret_cast<void*>(Lock);if(!std::strcmp(n,s::kUnlock))return reinterpret_cast<void*>(Unlock);
  if(!std::strcmp(n,abi::common::kSuspendConstruct)||!std::strcmp(n,"_ZN3art16ScopedSuspendAllC1EPKcb"))return reinterpret_cast<void*>(stop_proxy?stop_proxy:Stop);
  if(!std::strcmp(n,abi::common::kSuspendDestroy)||!std::strcmp(n,"_ZN3art16ScopedSuspendAllD1Ev"))return reinterpret_cast<void*>(resume_proxy?resume_proxy:Resume);
  if(!std::strcmp(n,"_ZN3art6Thread25ClearAllInterpreterCachesEv"))return reinterpret_cast<void*>(PrepareCache);
  if(!std::strcmp(n,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure"))return &closure;
  if(!std::strcmp(n,"_ZN3art5Locks17thread_list_lock_E"))return &list_lock;
  if(!std::strcmp(n,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE"))return reinterpret_cast<void*>(ListLock);
  if(!std::strcmp(n,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"))return reinterpret_cast<void*>(ListUnlock);
  if(!std::strcmp(n,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_"))return reinterpret_cast<void*>(ForEach);
  return delegate.FindSymbol(lib,n);
 }
 void*Install(void*t,void*p,void**o,std::string*)override{assert(stopped);*o=t;
  if(t==reinterpret_cast<void*>(Stop))stop_proxy=reinterpret_cast<abi::common::SuspendConstruct>(p);
  else if(t==reinterpret_cast<void*>(Resume))resume_proxy=reinterpret_cast<abi::common::SuspendDestroy>(p);
  else if(t==reinterpret_cast<void*>(Start))start_proxy=reinterpret_cast<s::StartGC>(p);
  else if(t==reinterpret_cast<void*>(Finish))finish_proxy=reinterpret_cast<s::FinishGC>(p);
  else if(t==reinterpret_cast<void*>(Disable))disable_proxy=reinterpret_cast<Op>(p);
  else if(t==reinterpret_cast<void*>(Enable))enable_proxy=reinterpret_cast<Op>(p);
  else std::abort();return t;
 }
};
struct OwnerHooks:early_owner_fixture::Hooks{
 void*FindSymbol(const char*lib,const char*n)override{if(!std::strcmp(n,"_ZN3art15instrumentation15Instrumentation10DeoptimizeEPNS_9ArtMethodE"))return reinterpret_cast<void*>(Deopt);if(!std::strcmp(n,"_ZN3art15instrumentation15Instrumentation12UndeoptimizeEPNS_9ArtMethodE"))return reinterpret_cast<void*>(Undeopt);if(!std::strcmp(n,"_ZN3art15instrumentation15Instrumentation13IsDeoptimizedEPNS_9ArtMethodE"))return reinterpret_cast<void*>(Query);return early_owner_fixture::Hooks::FindSymbol(lib,n);}
};
void InitOwner(){using namespace early_owner_fixture;mode="success";early_owner_fixture::thread_list=&::thread_list;class_linker=&::linker;
 static JNIInvokeInterface_ invocation{};invocation.GetEnv=GetEnv;invocation.AttachCurrentThread=AttachPublic;invocation.DetachCurrentThread=DetachPublic;vm.functions=&invocation;
 static jvmtiInterface_1_ functions{};functions.AddCapabilities=AddCapabilities;functions.SetEventCallbacks=SetCallbacks;functions.SetEventNotificationMode=Events;functions.DisposeEnvironment=Dispose;owned_env.functions=&functions;
 static JNINativeInterface_ native{};native.ExceptionCheck=ExceptionCheck;native.GetJavaVM=PublicVm;native.ExceptionClear=ExceptionClear;native.FindClass=FindClass;native.GetStaticMethodID=GetMethod;native.CallStaticObjectMethodV=CallStatic;native.NewGlobalRef=early_owner_fixture::Global;native.DeleteGlobalRef=DeleteGlobal;native.DeleteLocalRef=DeleteLocal;jni.functions=&native;
 abi::arm64::android15::RuntimePrefix prefix{};prefix.class_linker=class_linker;prefix.jni_id_manager=jni_manager;prefix.thread_list=early_owner_fixture::thread_list;prefix.java_vm=&vm;std::memcpy(runtime_storage,&prefix,sizeof(prefix));
}
}

namespace drain_fixture {
struct Environment final {
 Hooks hooks;OwnerHooks owner_hooks;
 modern_field_metadata::FieldMetadata metadata;method_identity::Calibration model;
 bounded_managed_roots::Store store{{nullptr,RootScope,RootOracle,RootDecode,RootElement}};
 bounded_managed_roots::Slot root;native_cache::Service threads;native_source_stop::Service source;
 int driver=0,body=0,holder=0;
 void Prepare(std::string&error){
 InitJni();InitializeHeap();InterfaceFixtures();
 assert(metadata.Bind(hooks,&error));std::array<modern_field_metadata::FieldOracle,4>field_oracles{};for(size_t i=0;i<4;++i)field_oracles[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i?1u:8u};
 struct FieldCal{modern_field_metadata::FieldMetadata&metadata;decltype(field_oracles)&oracles;std::string&error;}field_cal{metadata,field_oracles,error};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),[](const runnable_jni::Context&c,void*p){auto&r=*static_cast<FieldCal*>(p);return r.metadata.Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracles.data(),4,&r.error);},&field_cal)==runnable_jni::Result::kSuccess);
 Methods(old_class,false);std::array<method_identity::Oracle,3>oracles{};
 for(size_t i=0;i<3;++i){auto*m=reinterpret_cast<abi::arm64::android12::Method12*>(reinterpret_cast<char*>(old_method)+i*32);executable[i]=new(heap.New(sizeof(executable_mirror::Declaration)))executable_mirror::Declaration{};executable[i]->art_method=uintptr_t(m);executable[i]->dex_method_index=i;executable[i]->access_flags=i==0?0x101:i==1?1:9;oracles[i]={reinterpret_cast<jobject>(executable[i]),uint32_t(i),executable[i]->access_flags};}
 Put32(thread.data(),0,0);runnable_jni::Context context{thread.data(),&env,&::runtime};assert(method_identity::Calibrate(context,Leaf,oracles.data(),3,&model,&error));
 table.NewObjectArray=drain_fixture::RootArray;table.SetObjectArrayElement=drain_fixture::RootSet;table.GetObjectArrayElement=drain_fixture::RootGet;table.GetArrayLength=drain_fixture::RootLength;
 assert(store.Stage(&env,reinterpret_cast<jobject>(old_class),&root,&error));
 InitOwner();
 Put32(thread.data(),0,92u<<24);assert(threads.Bind(hooks,&thread_list,&error)&&threads.Prepare(&error));assert(native_source_stop::Install(hooks,&error));
 thread_suspend_service::Service states;assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&states));assert(native_source_stop::Service::Bind(hooks,&gc_heap,states,&source,&error));
 Put32(thread.data(),0,0);
 {native_source_stop::Observation observation(source,&driver,&body,thread.data(),thread.size());assert(observation.Collected(&driver,&holder));disable_proxy(&gc_heap,thread.data());Native(thread.data());unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);resume_proxy(&carrier);Runnable(thread.data());enable_proxy(&gc_heap,thread.data());native_source_stop::Calibration proof;assert(observation.Finish(&driver,true,&proof)&&source.Authorize(proof,&body));}
 }
};
}

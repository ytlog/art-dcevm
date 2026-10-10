#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_required_cleanup.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
namespace artdcevm::redefinition_gate {bool Owned(JavaVM*) noexcept{return current==thread.data();}}
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
std::array<void*,8> retired_records{};size_t retired_count=0;
bool OwnerRetired(void* m){return std::find(retired_records.begin(),retired_records.begin()+retired_count,m)!=retired_records.begin()+retired_count;}
void OwnerRetire(void*,void* m){assert(stopped&&retired_count<retired_records.size()&&!OwnerRetired(m));retired_records[retired_count++]=m;++retired_methods;}
int owner_heap,owner_mutex;void* owner_mutator=&owner_mutex;bool gc=false;int disabled=0;
abi::common::SuspendConstruct owner_stop_proxy=nullptr;abi::common::SuspendDestroy owner_resume_proxy=nullptr;
s::MutatorOperation owner_disable_proxy=nullptr,owner_enable_proxy=nullptr;
s::StartGC owner_start_proxy=nullptr;s::FinishGC owner_finish_proxy=nullptr;
void OwnerNative(void* p){assert(p==thread.data()&&!stopped&&Read32(p)==0);Put32(p,0,92u<<24);}
void OwnerRunnable(void* p){assert(p==thread.data()&&!stopped&&Read32(p)==92u<<24);Put32(p,0,0);}
void OwnerDisable(void* h,void* p){assert(h==&owner_heap&&p==thread.data()&&!stopped&&!disabled&&Read32(p)==0);++disabled;}
void OwnerEnable(void* h,void* p){assert(h==&owner_heap&&p==thread.data()&&!stopped&&disabled==1&&Read32(p)==0);--disabled;}
void OwnerStart(void* h,void* p,int32_t cause,int32_t collector){assert(h==&owner_heap&&p==thread.data()&&cause==8&&collector==1&&!gc&&!stopped);gc=true;}
void OwnerFinish(void* h,void* p,int32_t type){assert(h==&owner_heap&&p==thread.data()&&type==0&&gc&&!stopped);gc=false;}
void OwnerStop(void*,const char*,bool){assert(Read32(thread.data())==92u<<24&&!stopped&&!locked);++stopped;}
void OwnerResume(void*){assert(Read32(thread.data())==92u<<24&&stopped==1&&!locked);--stopped;}
void OwnerLock(void* m,void* p){assert(m==&owner_mutex&&p==thread.data()&&gc&&!stopped);++stopped;}
void OwnerUnlock(void* m,void* p){assert(m==&owner_mutex&&p==thread.data()&&gc&&stopped==1);--stopped;}
struct Context final:abi::arm64::context::Prefix {
 void Reset()override{}~Context()override=default;void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t)override{return false;}uintptr_t* GetGPRAddress(uint32_t)override{return nullptr;}
 uintptr_t GetGPR(uint32_t)override{std::abort();}void SetGPR(uint32_t,uintptr_t)override{}
 bool IsAccessibleFPR(uint32_t)override{return false;}uintptr_t GetFPR(uint32_t)override{std::abort();}
};
abi::arm64::context::Prefix* OwnerContext(){assert(!stopped);return new Context;}
struct NativeMethod{uint32_t klass=0,flags=0x100;} native_frame;
void* quick_frame=&native_frame;
void OwnerConstruct(void* p,void* t,void* c,abi::arm64::stack::WalkKind k,bool check){assert(gc&&stopped&&locked&&check&&k==abi::arm64::stack::WalkKind::kIncludeInlined);auto* v=static_cast<abi::arm64::stack::Visitor13*>(p);std::memset(static_cast<unsigned char*>(p)+8,0,sizeof(*v)-8);v->thread=t;v->context=c;v->kind=k;}
void OwnerWalk(void* p,bool transitions){assert(gc&&stopped&&locked&&transitions);auto* v=static_cast<abi::arm64::stack::Visitor13*>(p);if(v->thread==foreign_thread.data())return;assert(v->thread==thread.data());v->quick_frame=&quick_frame;v->VisitFrame();}
void* OwnerMethod(const void*){return &native_frame;}
void Unused(){std::abort();}
void WrongVisitor(void*,void*){}
jint PublicVM(JNIEnv*,JavaVM** out){Jni();*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
class OwnerHooks:public ProviderHooks {
 public:void* FindSymbol(const char* lib,const char* name)override{
 if(missing&&std::strstr(name,missing))return nullptr;
#define MATCH(n,f) if(!std::strcmp(name,n))return reinterpret_cast<void*>(f)
 MATCH(abi::common::kSuspendConstruct,owner_stop_proxy?owner_stop_proxy:OwnerStop);MATCH("_ZN3art16ScopedSuspendAllC1EPKcb",owner_stop_proxy?owner_stop_proxy:OwnerStop);
 MATCH(abi::common::kSuspendDestroy,owner_resume_proxy?owner_resume_proxy:OwnerResume);MATCH("_ZN3art16ScopedSuspendAllD1Ev",owner_resume_proxy?owner_resume_proxy:OwnerResume);
 MATCH(abi::arm64::android13::kToNative,OwnerNative);MATCH(abi::arm64::android13::kToRunnable,OwnerRunnable);
 if(!std::strcmp(name,s::kMutator))return &owner_mutator;
 MATCH(s::kStartGC,owner_start_proxy?owner_start_proxy:OwnerStart);MATCH(s::kFinishGC,owner_finish_proxy?owner_finish_proxy:OwnerFinish);
 MATCH(s::kDisableMoving,owner_disable_proxy?owner_disable_proxy:OwnerDisable);MATCH(s::kEnableMoving,owner_enable_proxy?owner_enable_proxy:OwnerEnable);
 MATCH(s::kLock,OwnerLock);MATCH(s::kUnlock,OwnerUnlock);
 MATCH(abi::arm64::context::kCreate,OwnerContext);MATCH(abi::arm64::stack::kConstruct,OwnerConstruct);MATCH(abi::arm64::stack::kWalk,OwnerWalk);MATCH(abi::arm64::stack::kMethod,OwnerMethod);
 if(!std::strcmp(name,abi::arm64::android12::frame_registers::kFast)||!std::strcmp(name,abi::arm64::android14::frame_registers::kNativePcOffset)||!std::strcmp(name,abi::arm64::android13::kConstructCodeInfo)||!std::strcmp(name,abi::arm64::android13::kFindStackMap)||!std::strcmp(name,abi::arm64::dex_register_map::kDecode)||std::strstr(name,"GetDexPc")||std::strstr(name,"HasShouldDeoptimizeFlag")||std::strstr(name,"ContainsPc")||std::strstr(name,"IsNterpMethodHeader"))return reinterpret_cast<void*>(Unused);
#undef MATCH
 return ProviderHooks::FindSymbol(lib,name);
 }
 void* Install(void* target,void* proxy,void** original,std::string* e)override{
  assert(stopped&&Read32(thread.data())==92u<<24);*original=target;
#define INSTALL(f,p,T) if(target==reinterpret_cast<void*>(f)){p=reinterpret_cast<T>(proxy);return target;}
 INSTALL(OwnerStop,owner_stop_proxy,abi::common::SuspendConstruct);INSTALL(OwnerResume,owner_resume_proxy,abi::common::SuspendDestroy);
 INSTALL(OwnerDisable,owner_disable_proxy,s::MutatorOperation);INSTALL(OwnerEnable,owner_enable_proxy,s::MutatorOperation);
 INSTALL(OwnerStart,owner_start_proxy,s::StartGC);INSTALL(OwnerFinish,owner_finish_proxy,s::FinishGC);
#undef INSTALL
 return ProviderHooks::Install(target,proxy,original,e);
 }
};
jobject Register(void*,void* p){assert(!stopped);return reinterpret_cast<jobject>(p);}
bool Resolve(void*,jobject p,void** out){*out=p;return bool(p);}
void Release(void*,jobject){assert(!stopped);}
}
int main(int argc,char** argv){
#include "provider_setup.inc"
 dependencies.is_retired=OwnerRetired;dependencies.retirement.retire_method=OwnerRetire;
 events.reserve(128);
 table.GetJavaVM=PublicVM;dependencies.runtime.receivers.java_vm=reinterpret_cast<JavaVM*>(&runtime);
 native_required_cleanup::Binding closure;assert(native_required_cleanup::Binding::Bind(hooks,&env,dependencies,&closure,&error));
 auto invalid=dependencies;invalid.runtime.runtime=&linker;native_required_cleanup::Binding rejected;assert(!native_required_cleanup::Binding::Bind(hooks,&env,invalid,&rejected,&error));
 invalid=dependencies;invalid.runtime.receivers.java_vm=&linker;assert(!native_required_cleanup::Binding::Bind(hooks,&env,invalid,&rejected,&error));
 for(auto missing:{"SetStatusLocked","SetObsoleteClass","InvalidateAllCompiledCode","DeoptimizeAllThreadFrames","DeoptimizeEverything","ReplaceReferences","VisitReflectiveTargets","CurrentFromGdb"}){hooks.missing=missing;assert(!native_required_cleanup::Binding::Bind(hooks,&env,dependencies,&rejected,&error));}hooks.missing=nullptr;
 current=foreign_thread.data();assert(!native_required_cleanup::Binding::Bind(hooks,&env,dependencies,&rejected,&error));current=thread.data();
 assert(native_source_stop::Install(hooks,&error));
 thread_suspend_service::Service states;assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&states));
 native_source_stop::Service source;assert(native_source_stop::Service::Bind(hooks,&owner_heap,states,&source,&error));
 int driver_marker,original_run;uint32_t root_slot=0;transaction_root::Holder holder{};
 Put32(thread.data(),0,0);
 {native_source_stop::Observation observation(source,&driver_marker,&original_run,thread.data(),thread.size());assert(observation.Collected(&driver_marker,&holder));
 owner_disable_proxy(&owner_heap,thread.data());OwnerNative(thread.data());unsigned char carrier;owner_stop_proxy(&carrier,s::kFinalCause,true);owner_resume_proxy(&carrier);OwnerRunnable(thread.data());owner_enable_proxy(&owner_heap,thread.data());native_source_stop::Calibration proof;assert(observation.Finish(&driver_marker,true,&proof)&&source.Authorize(proof,&original_run));}
 whole_process_frames::Service scanner;assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid14,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&scanner));
 whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage));
 prepared_roots::Registry registry({nullptr,Register,Resolve,Release});size_t slot;
 for(auto p:{static_cast<void*>(old_class),static_cast<void*>(new_class),old_instance,new_instance,parent_class,meta_class,class_ext,class_array,ref_a,ref_b})assert(registry.Add(p,&slot));
 assert(registry.Freeze()&&registry.Resolve());
 native_source_stop::Session session(source);native_member_provider::Service provider;
 abi::arm64::android_common::RedefinerPrefix driver{};driver.self=thread.data();driver.runtime=dependencies.runtime.runtime;driver.environment=&runtime;
 std::vector<unsigned char> owners(sizeof(abi::arm64::android15::ClassRedefinition));reinterpret_cast<abi::arm64::android_common::ClassRedefinitionPrefix*>(owners.data())->driver=&driver;
 transaction_root::Owners range{owners.data(),owners.data()+owners.size(),owners.data()+owners.size()};uint64_t bits=1;
 holder.array_handle=&root_slot;holder.redefinitions=&range;holder.initialized={&bits,1,1};holder.actually_structural={&bits,1,1};holder.initial_structural={&bits,1,1};
 transaction_root::Metadata actual;assert(transaction_root::ReadMetadata(holder,1,transaction_root::OwnerFamily::kAndroid15,&actual));
 assert(scanner.PrepareRecoveryPlan(plan,&storage));
 native_required_cleanup::Service cleanup(closure,thread.data(),&driver,session,scanner,storage,caches,&jit_cache,provider,registry);
 auto wrong_metadata=actual;wrong_metadata.initialized.capacity=2;assert(!cleanup.AttachHolder(&holder,wrong_metadata));
 assert(!cleanup.AttachHolder(reinterpret_cast<transaction_root::Holder*>(0x1000),actual));
 auto* owner=reinterpret_cast<abi::arm64::android_common::ClassRedefinitionPrefix*>(owners.data());owner->driver=&driver_marker;assert(!cleanup.AttachHolder(&holder,actual));owner->driver=&driver;
 driver.runtime=&linker;assert(!cleanup.AttachHolder(&holder,actual));driver.runtime=dependencies.runtime.runtime;
 driver.environment=nullptr;assert(!cleanup.AttachHolder(&holder,actual));driver.environment=&runtime;
 driver.type=1;assert(!cleanup.AttachHolder(&holder,actual));driver.type=0;
 assert(cleanup.AttachHolder(&holder,actual)&&!cleanup.AttachHolder(&holder,actual));
 assert(!cleanup.PrepareCollected(plan,&error));assert(session.Begin(thread.data(),thread.size()));
 modern_field_metadata::ReadContext recovery_context{&session,[](void*raw,const void*p,size_t*out)noexcept {if(!static_cast<native_source_stop::Session*>(raw)->IsStopped(thread.data()))return false;return method_identity::ReadableMappingExtent(p,out);}};
 assert(!cleanup.PrepareCollected(plan,&error)&&error=="Cleanup frame recovery lacks ordinary exact Plan preparation and stopped read context"&&!cleanup.RecoveryProof().ValidateFinal(plan));
 assert(cleanup.UseRecoveryContext(recovery_context));
 assert(!cleanup.IsStopped(&driver_marker,&holder));assert(!cleanup.Verify(&driver,&holder,plan));
 native_frame.flags=0;assert(!cleanup.PrepareCollected(plan,&error));native_frame.flags=0x100;
 assert(cleanup.PrepareCollected(plan,&error)&&cleanup.Verify(&driver,&holder,plan));
 driver.runtime=&linker;assert(!cleanup.Verify(&driver,&holder,plan));driver.runtime=dependencies.runtime.runtime;
 driver.environment=nullptr;assert(!cleanup.Verify(&driver,&holder,plan));driver.environment=&runtime;
 owner->driver=&driver_marker;assert(!cleanup.Verify(&driver,&holder,plan));owner->driver=&driver;
 holder.array_handle=reinterpret_cast<uint32_t*>(0x1000);assert(!cleanup.Verify(&driver,&holder,plan));holder.array_handle=&root_slot;
 if(std::getenv("ARTDCEVM_TEST_WRONG_VISITOR")){size_t registered=0;caches.VisitStoppedWorld(reinterpret_cast<native_cache::Service::StoppedVisitor>(WrongVisitor),nullptr,&registered,&error);std::abort();}
 auto foreign_plan=plan;assert(!cleanup.Verify(&driver,&holder,foreign_plan));
 current=foreign_thread.data();assert(!cleanup.Verify(&driver,&holder,plan));current=thread.data();
 Methods(new_class,true);LinkDex(new_class,dex[1]);dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;
 auto native=provider.Prepare(plan,dependencies,&error);if(!native){std::cerr<<error<<'\n';return 1;}auto* concrete=dynamic_cast<native_source_commit::Services*>(native.get());assert(concrete);
 native_member_provider::Service other;assert(!other.OwnsPreparedSource(plan,*concrete)&&!provider.OwnsPreparedSource(foreign_plan,*concrete)&&provider.OwnsPreparedSource(plan,*concrete));
 {auto foreign=other.Prepare(plan,dependencies,&error);assert(foreign);auto* foreign_native=dynamic_cast<native_source_commit::Services*>(foreign.get());assert(foreign_native&&!provider.OwnsPreparedSource(plan,*foreign_native));provider.ReleasePreparedSource(*foreign_native);assert(provider.OwnsPreparedSource(plan,*concrete));assert(!cleanup.Prepare(std::move(foreign),plan,&error)&&provider.OwnsPreparedSource(plan,*concrete));}
 assert(provider.CollectManagedRoots(*native,registry.Existing()));
 auto facade=cleanup.Prepare(std::move(native),plan,&error);if(!facade){std::cerr<<error<<'\n';return 1;}
 assert(!facade->Validate(plan)&&!cleanup.CommitObserver(plan)&&!cleanup.Completed());
 assert(session.ContinueSource());registry.Invalidate();assert(!cleanup.Verify(&driver,&holder,plan));
 owner_disable_proxy(&owner_heap,thread.data());OwnerNative(thread.data());unsigned char carrier;owner_stop_proxy(&carrier,s::kFinalCause,true);
 forbid_provider_allocation=true;
 assert(registry.Resolve()&&provider.RefreshManagedRoots(plan,facade->Delegate(),registry.Final())&&cleanup.RefreshFinal(registry.Final()));
 assert(!cleanup.RecoveryProof().ValidateFinal(plan));
 assert(!cleanup.CommitObserver(plan)&&!facade->Validate(foreign_plan)&&facade->Validate(plan)&&cleanup.RecoveryProof().ValidateFinal(plan));
 assert(!cleanup.CommitObserver(foreign_plan));
 auto observer=[](const source_commit::Plan& p,void* raw) noexcept{auto& service=*static_cast<native_required_cleanup::Service*>(raw);assert(service.CommitObserver(p)&&!service.CommitObserver(p)&&!service.Completed());service.BeginCommit(p);};
 assert(source_commit::Run(plan,*facade,{&cleanup,observer})&&cleanup.Completed());
 forbid_provider_allocation=false;
 assert(Read32(new_class,128)==73&&Read32(new_instance,8)==31&&retired_methods==1&&cleared==2&&gc&&stopped==1);
 provider.ReleasePreparedSource(*concrete);assert(!provider.OwnsPreparedSource(plan,*concrete));facade.reset();
 owner_resume_proxy(&carrier);OwnerRunnable(thread.data());owner_enable_proxy(&owner_heap,thread.data());assert(session.Completed());registry.Clear();
 std::cout<<"actual Required cleanup owner: full native delegate, phases, closure/owner/self/Holder identity, preRegistered roots, single original Run sequence PASS\n";
}

#include "platform/common/native_required_cleanup.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/redefinition_gate.h"
#include <cstdlib>
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
namespace artdcevm::native_required_cleanup {
namespace {bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}void Require(bool v){if(!v)std::abort();}}
bool Binding::Bind(HookEngine& hooks,JNIEnv* env,const native_member_provider::Dependencies& d,Binding* out,std::string* error){
 if(out)*out={};if(!out||!env||env->ExceptionCheck()||!d.metadata||!d.fields||!d.cache||!d.reflection||!d.method_model||!d.metadata->Calibrated()||!d.fields->Calibrated()||!d.cache->PreparedForCurrentThread()||!d.runtime.runtime||!d.runtime.instrumentation||!d.runtime.receivers.thread_list||!d.runtime.receivers.jit||!d.runtime.receivers.jit_code_cache)return Fail(error,"Cleanup closure lacks actual native process owners");
 Binding b;if(env->GetJavaVM(&b.vm_)!=JNI_OK||!b.vm_||env->ExceptionCheck()||d.runtime.receivers.java_vm!=b.vm_)return Fail(error,"Cleanup closure public VM differs from native receiver");
 b.current_=reinterpret_cast<decltype(b.current_)>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
 constexpr const char* names[]={"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE","_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE","_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv","_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv","_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc","_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE","_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE","_ZN3art6Thread14CurrentFromGdbEv"};
 for(size_t i=0;i<b.symbols_.size();++i){b.symbols_[i]=hooks.FindSymbol(i==5?"libopenjdkjvmti.so":"libart.so",names[i]);if(!b.symbols_[i])return Fail(error,"Cleanup closure is missing a fixed named native consumer");}
 if(!b.current_||!b.current_()||d.reflection->runtime_!=d.runtime.runtime||d.reflection->visit_!=b.symbols_[6])return Fail(error,"Cleanup closure differs from actual reflective runtime binding");
 b.dependencies_=d;*out=b;return true;
}
bool Binding::Current(JavaVM* vm,void* self)const noexcept{return vm_&&vm==vm_&&current_&&self&&current_()==self&&dependencies_.cache&&dependencies_.cache->PreparedForCurrentThread();}
bool Binding::Matches(const native_source_commit::Services& s,const native_member_provider::Service& p)const noexcept{
 const auto& d=s.dependencies_;return p.reflection_==dependencies_.reflection&&d.reflective.state==&p&&d.reflective.prepared==native_member_provider::Service::ReflectReady&&d.reflective.update==native_member_provider::Service::ReflectUpdate&&d.reflective.begin_commit==native_member_provider::Service::ReflectRetire&&s.self_&&Current(vm_,s.self_)&&d.metadata==dependencies_.metadata&&d.fields==dependencies_.fields&&d.caches==dependencies_.cache&&d.observed.runtime==dependencies_.runtime.runtime&&d.observed.instrumentation==dependencies_.runtime.instrumentation&&d.observed.receivers.java_vm==vm_&&d.observed.receivers.thread_list==dependencies_.runtime.receivers.thread_list&&d.observed.receivers.jit==dependencies_.runtime.receivers.jit&&d.observed.receivers.jit_code_cache==dependencies_.runtime.receivers.jit_code_cache&&reinterpret_cast<void*>(s.set_status_)==symbols_[0]&&reinterpret_cast<void*>(s.retain_)==symbols_[1]&&reinterpret_cast<void*>(s.invalidate_)==symbols_[2]&&reinterpret_cast<void*>(s.deopt_frames_)==symbols_[3]&&reinterpret_cast<void*>(s.deopt_everything_)==symbols_[4]&&reinterpret_cast<void*>(s.replace_)==symbols_[5];
}
Service::Service(const Binding& binding,void* self,void* driver,
 native_source_stop::Session& stop,const whole_process_frames::Service& frames,
 whole_process_frames::Service::Prepared& storage,const native_cache::Service& cache,
 void* jit,native_member_provider::Service& provider,prepared_roots::Registry& registry)
 :binding_(binding),vm_(binding.vm_),self_(self),driver_(driver),stop_(stop),frames_(frames),frame_storage_(storage),cache_(cache),jit_(jit),provider_(provider),registry_(registry){}
bool Service::AttachHolder(const transaction_root::Holder* h,const transaction_root::Metadata& m) noexcept{
 auto mapped=[](const void* p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;};
 if(phase_!=Phase::kEmpty||holder_||!h||!m.count||!m.stride||stop_.Reserved()||!binding_.Current(vm_,self_)||!redefinition_gate::Owned(vm_)||!mapped(driver_,sizeof(abi::arm64::android_common::RedefinerPrefix))||!mapped(h,sizeof(*h)))return false;
 abi::arm64::android_common::RedefinerPrefix driver;std::memcpy(&driver,driver_,sizeof(driver));if(driver.self!=self_||driver.runtime!=binding_.dependencies_.runtime.runtime||!driver.environment||driver.type!=0)return false;
 transaction_root::Holder header;transaction_root::Metadata actual;
 if(!transaction_root::ReadHolderHeader(h,sizeof(*h),&header)||!mapped(header.array_handle,sizeof(uint32_t))||!mapped(header.redefinitions,sizeof(transaction_root::Owners)))return false;
 bool family=false;
 for(auto f:{transaction_root::OwnerFamily::kAndroid11,transaction_root::OwnerFamily::kAndroid13,transaction_root::OwnerFamily::kAndroid15}){
  transaction_root::Metadata candidate;if(transaction_root::ReadMetadata(header,m.count,f,&candidate)&&candidate.stride==m.stride){actual=candidate;family=true;break;}
 }
 auto bits=[](transaction_root::Bits a,transaction_root::Bits b){return a.begin==b.begin&&a.size==b.size&&a.capacity==b.capacity;};
 if(!family||actual.owners.begin!=m.owners.begin||actual.owners.end!=m.owners.end||actual.owners.capacity!=m.owners.capacity||!bits(actual.initialized,m.initialized)||!bits(actual.actually_structural,m.actually_structural)||!bits(actual.initial_structural,m.initial_structural)||!mapped(actual.owners.begin,size_t(uintptr_t(actual.owners.capacity)-uintptr_t(actual.owners.begin))))return false;
 for(auto b:{actual.initialized,actual.actually_structural,actual.initial_structural})if(!mapped(b.begin,b.capacity*sizeof(uint64_t)))return false;
 for(size_t i=0;i<actual.count;++i){const void* owner=actual.owners.begin+i*actual.stride;abi::arm64::android_common::ClassRedefinitionPrefix prefix;std::memcpy(&prefix,owner,sizeof(prefix));if(prefix.driver!=driver_)return false;}
 holder_=h;holder_header_=header;holder_metadata_=actual;return true;
}
Service::~Service(){Require(!delegate_);}
bool Service::HolderCurrent()const noexcept{
 // Source Run owns this mapped native storage until its original final scope
 // returns. Never dereference it unless the SAME Session proves that stop.
 transaction_root::Holder header;std::memcpy(&header,holder_,sizeof(header));
 if(std::memcmp(&header,&holder_header_,sizeof(header)))return false;
 transaction_root::Owners owners;std::memcpy(&owners,header.redefinitions,sizeof(owners));
 if(owners.begin!=holder_metadata_.owners.begin||owners.end!=holder_metadata_.owners.end||owners.capacity!=holder_metadata_.owners.capacity)return false;
 abi::arm64::android_common::RedefinerPrefix driver;std::memcpy(&driver,driver_,sizeof(driver));if(driver.self!=self_||driver.runtime!=binding_.dependencies_.runtime.runtime||!driver.environment||driver.type!=0)return false;
 for(size_t i=0;i<holder_metadata_.count;++i){abi::arm64::android_common::ClassRedefinitionPrefix owner;std::memcpy(&owner,owners.begin+i*holder_metadata_.stride,sizeof(owner));if(owner.driver!=driver_)return false;}
 return true;
}
bool Service::IsStopped(void* d,const transaction_root::Holder* h)const noexcept{
 return vm_&&self_&&driver_&&holder_&&&cache_==binding_.dependencies_.cache&&jit_==binding_.dependencies_.runtime.receivers.jit_code_cache&&d==driver_&&h==holder_&&redefinition_gate::Owned(vm_)&&binding_.Current(vm_,self_)&&stop_.IsStopped(self_)&&HolderCurrent();
}
bool Service::IsFinalWindow(void* d,const transaction_root::Holder* h)const noexcept{return IsStopped(d,h)&&stop_.IsFinalWindow(self_);}
bool Service::Exact(const source_commit::Plan& p)const noexcept{return plan_==&p&&source_commit::SameIdentity(p,expected_);}
bool Service::UseRecoveryContext(const modern_field_metadata::ReadContext&context) noexcept{
 if(phase_!=Phase::kEmpty||!stop_.IsCollectedStop(self_)||!context.owner||!context.extent||!frame_storage_.recovery_plan||(recovery_context_&&recovery_context_!=&context))return false;
 recovery_context_=&context;return true;
}
bool Service::PrepareCollected(source_commit::Plan& p,std::string* error){
 if(phase_!=Phase::kEmpty||!IsStopped(driver_,holder_)||!stop_.IsCollectedStop(self_)||!cache_.PreparedForCurrentThread())return Fail(error,"Cleanup requires actual Collected stop and bound native closure");
 if(!recovery_context_||frame_storage_.recovery_plan!=&p)return Fail(error,"Cleanup frame recovery lacks ordinary exact Plan preparation and stopped read context");
 expected_=p;roots_=prepared_roots::Bindings{};if(!rooted_schema_plan::CollectBindings(&expected_,registry_.Existing(),&roots_))return Fail(error,"Cleanup Plan has an unregistered managed root");
 frames_report_=frames_.ScanRecovery(stop_,self_,cache_,jit_,frame_storage_,p,*recovery_context_,&recovery_proof_);
 if(!frames_report_.accepted)return Fail(error,frames_report_.failure);
 plan_=&p;phase_=Phase::kCollected;return true;
}
std::unique_ptr<Facade> Service::Prepare(std::unique_ptr<source_commit::Services> incoming,source_commit::Plan& p,std::string* error){
 auto* concrete=dynamic_cast<native_source_commit::Services*>(incoming.get());
 if(phase_!=Phase::kCollected||delegate_||!concrete||!IsStopped(driver_,holder_)||!stop_.IsCollectedStop(self_)||!Exact(p)||!provider_.OwnsPreparedSource(p,*concrete)||!binding_.Matches(*concrete,provider_)){
  if(concrete)provider_.ReleasePreparedSource(*concrete);
  Fail(error,"Cleanup requires same actual prepared native provider and source closure");return nullptr;
 }
 std::unique_ptr<native_source_commit::Services> delegate(static_cast<native_source_commit::Services*>(incoming.release()));
 delegate_=delegate.get();
 if(!delegate_->Validate(p)){provider_.ReleasePreparedSource(*delegate_);delegate_=nullptr;Fail(error,"Actual native/reflection cleanup preparation refused");return nullptr;}
 try{return std::unique_ptr<Facade>(new Facade(*this,std::move(delegate)));}
 catch(...){provider_.ReleasePreparedSource(*delegate_);delegate_=nullptr;throw;}
}
bool Service::RefreshFinal(const prepared_roots::Resolver& resolver) noexcept{
 if(phase_!=Phase::kCollected||!IsFinalWindow(driver_,holder_)||!delegate_||!roots_.CanRefresh(resolver))return false;
 roots_.Refresh(resolver);
 if(!Exact(*plan_)||!provider_.OwnsPreparedSource(*plan_,*delegate_))return false;
 phase_=Phase::kFinalRefreshed;return true;
}
bool Service::Verify(void* d,const transaction_root::Holder* h,const source_commit::Plan& p)const noexcept{
 if(!IsStopped(d,h)||!Exact(p)||!frames_report_.accepted)return false;
 if(stop_.IsCollectedStop(self_))return phase_==Phase::kCollected;
 return delegate_&&binding_.Matches(*delegate_,provider_)&&provider_.OwnsPreparedSource(p,*delegate_)&&stop_.IsFinalWindow(self_)&&(phase_==Phase::kFinalRefreshed||phase_==Phase::kValidated||phase_==Phase::kObserver||phase_==Phase::kLedger||phase_==Phase::kDelegating);
}
bool Service::ValidateFinal(const source_commit::Plan& p)const{
 if((phase_!=Phase::kFinalRefreshed&&phase_!=Phase::kValidated)||!Verify(driver_,holder_,p)||!IsFinalWindow(driver_,holder_))return false;
 // The full original frame walker runs again in the actual final transfer,
 // including every registered thread and inline/compiled location proof.
 frames_report_=frames_.ScanRecovery(stop_,self_,cache_,jit_,frame_storage_,p,*recovery_context_,&recovery_proof_);
 if(!frames_report_.accepted||!recovery_proof_.ValidateFinal(p)||!delegate_->Validate(p))return false;
 phase_=Phase::kValidated;return true;
}
bool Service::CommitObserver(const source_commit::Plan& p) noexcept{
 if(phase_!=Phase::kValidated||!IsFinalWindow(driver_,holder_)||!Exact(p))return false;
 phase_=Phase::kObserver;return true;
}
void Service::BeginCommit(const source_commit::Plan& p)const noexcept{
 Require(phase_==Phase::kObserver&&IsFinalWindow(driver_,holder_)&&Exact(p));phase_=Phase::kLedger;
}
void Service::Finish(const source_commit::Plan& p)const noexcept{
 // ReplaceReferences may rewrite managed roots. No old Plan/GC root decoding
 // is performed after mutation; the native Run identities remain authoritative.
 Require(plan_==&p&&phase_==Phase::kDelegating&&IsFinalWindow(driver_,holder_));phase_=Phase::kCompleted;
}
bool Service::Completed()const noexcept{return phase_==Phase::kCompleted;}
Facade::Facade(Service& owner,std::unique_ptr<native_source_commit::Services> delegate):owner_(owner),delegate_(std::move(delegate)){}
Facade::~Facade(){Require(owner_.delegate_==delegate_.get());owner_.provider_.ReleasePreparedSource(*delegate_);owner_.delegate_=nullptr;}
bool Facade::Validate(const source_commit::Plan& p)const{return owner_.ValidateFinal(p);}
void Facade::BeginCommit(const source_commit::Plan& p)const{
 Require(owner_.phase_==Service::Phase::kLedger&&owner_.IsFinalWindow(owner_.driver_,owner_.holder_)&&owner_.Exact(p));
 owner_.phase_=Service::Phase::kDelegating;delegate_->BeginCommit(p);
}
void Facade::RequireMutation()const noexcept{Require(owner_.phase_==Service::Phase::kDelegating&&owner_.IsFinalWindow(owner_.driver_,owner_.holder_));}
uint32_t Facade::Status(void* k)const{RequireMutation();return delegate_->Status(k);}
void Facade::SetStatus(void* k,uint32_t s)const{RequireMutation();delegate_->SetStatus(k,s);}
void Facade::CopyFields(bool stat,void* t,void* tc,void* s,void* sc)const{
 RequireMutation();Require(cleanup_step_==0);
 const size_t n=stat?static_copies_:instance_copies_;const auto& old=stat?owner_.plan_->old_classes:owner_.plan_->old_instances;const auto& next=stat?owner_.plan_->new_classes:owner_.plan_->new_instances;
 Require(n<old.size()&&s==old[n]&&t==next[n]&&(!stat||(tc==t&&sc==s)));delegate_->CopyFields(stat,t,tc,s,sc);++(stat?static_copies_:instance_copies_);
}
void* Facade::ObjectClass(void* o)const{RequireMutation();return delegate_->ObjectClass(o);}
void Facade::MarkClassAndMethodsObsolete(void* old,void* next)const{
 RequireMutation();Require(cleanup_step_==0&&static_copies_==owner_.plan_->old_classes.size()&&instance_copies_==owner_.plan_->old_instances.size());Require(obsolete_<owner_.plan_->old_classes.size()&&old==owner_.plan_->old_classes[obsolete_]&&next==owner_.plan_->new_classes[obsolete_]);delegate_->MarkClassAndMethodsObsolete(old,next);++obsolete_;
}
void Facade::UpdateReflectiveReferences(const source_commit::Plan& p)const{
 RequireMutation();Require(&p==owner_.plan_&&cleanup_step_==0&&obsolete_==p.old_classes.size());delegate_->UpdateReflectiveReferences(p);cleanup_step_=1;
}
void Facade::ForceDeoptimizeFrames()const{RequireMutation();Require(cleanup_step_==1);delegate_->ForceDeoptimizeFrames();cleanup_step_=2;}
void Facade::ReplaceObjectReferences()const{RequireMutation();Require(cleanup_step_==2);delegate_->ReplaceObjectReferences();cleanup_step_=3;}
void Facade::RestoreDeclaringClasses(const source_commit::Plan& p)const{RequireMutation();Require(&p==owner_.plan_&&cleanup_step_==3);delegate_->RestoreDeclaringClasses(p);cleanup_step_=4;}
void* Facade::NewClassAfterReplacement(void* a,size_t n)const{RequireMutation();Require(cleanup_step_==4&&!pending_retained_&&retained_<owner_.plan_->old_classes.size());
 const auto& p=*owner_.plan_;Require(a==(p.class_root_origins.empty()?p.new_classes_root:p.class_root_origins[retained_].array)&&n==(p.class_root_origins.empty()?retained_:p.class_root_origins[retained_].index));pending_retained_=delegate_->NewClassAfterReplacement(a,n);Require(pending_retained_);return pending_retained_;}
void Facade::RetainOldClass(void* next,void* old)const{RequireMutation();Require(cleanup_step_==4&&pending_retained_==next&&retained_<owner_.plan_->old_classes.size()&&old==owner_.plan_->old_classes[retained_]);delegate_->RetainOldClass(next,old);pending_retained_=nullptr;++retained_;}
void Facade::InvalidateCompiledCode()const{RequireMutation();Require(cleanup_step_==4&&retained_==owner_.plan_->old_classes.size());delegate_->InvalidateCompiledCode();cleanup_step_=5;}
void Facade::DeoptimizeEverythingForAbstractTransition()const{RequireMutation();Require(cleanup_step_==5&&owner_.plan_->abstract_instances);delegate_->DeoptimizeEverythingForAbstractTransition();cleanup_step_=6;}
void Facade::ClearInterpreterCaches()const{
 RequireMutation();Require(cleanup_step_==(owner_.plan_->abstract_instances?6u:5u));delegate_->ClearInterpreterCaches();cleanup_step_=7;owner_.Finish(*owner_.plan_);
}
}

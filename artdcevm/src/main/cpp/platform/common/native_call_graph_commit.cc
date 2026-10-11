#include "platform/common/native_call_graph_commit.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/structural_methods.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/native_hierarchy_session.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace artdcevm::native_call_graph_commit {
namespace {
constexpr char kReplace[]="_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE";
constexpr char kStatus[]="_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE";
constexpr char kRetain[]="_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE";
// Fixed libdexfile/dex/modifiers.h class runtime bit; this scalar operation
// mirrors Class::SetObsoleteObject and stores no managed reference.
constexpr uint32_t kObsoleteObject=0x00200000u;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
}
bool Binding::Bind(HookEngine&hooks,const runtime_services::Snapshot&runtime,std::string*error){
 if(runtime_)return Fail(error,"call-graph-commit-binding-repeated");
 auto slot=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 auto replace=reinterpret_cast<Replace>(hooks.FindSymbol("libopenjdkjvmti.so",kReplace));
 auto status=reinterpret_cast<SetStatus>(hooks.FindSymbol("libart.so",kStatus));
 auto retain=reinterpret_cast<Retain>(hooks.FindSymbol("libart.so",kRetain));
 if(!runtime.runtime||!runtime.receivers.heap||!runtime.receivers.class_linker||!slot||*slot!=runtime.runtime||
    !replace||!status||!retain)return Fail(error,"call-graph-commit-fixed-original-operations-unavailable");
 runtime_=runtime.runtime;runtime_slot_=slot;replace_=replace;status_=status;retain_=retain;
 return true;
}
std::unique_ptr<Service>Service::Prepare(const runnable_jni::Context&context,
 source_commit::Plan&plan,const Dependencies&deps,std::string*error){
 const auto deny=[&](const char*why)->std::unique_ptr<Service>{Fail(error,why);return nullptr;};
 if(!deps.binding||!deps.binding->runtime_slot_||*deps.binding->runtime_slot_!=deps.binding->runtime_||
    !deps.metadata||!deps.metadata->Calibrated()||!deps.fields||!deps.fields->Calibrated()||
    !deps.reflection||!deps.reflection_plan||!deps.initialization||!deps.heap||!deps.heap->Calibrated()||
    !deps.root_provenance||!deps.root_provenance->LocksCalibrated()||
    !deps.weak_replacement||!deps.weak_replacement->BoundTo(deps.binding->runtime_)||!deps.cache||!deps.cache->Prepared()||
    !deps.schema||!deps.execution||!deps.interpretation||!deps.compilation||!deps.compilation_receipt||
    !deps.compilation->Validate(*deps.compilation_receipt,context.self)||
    !deps.stop||!deps.loading||!deps.read||!deps.read->owner||!deps.read->extent||
    !context.env||!context.vm||!context.self||context.self!=deps.self||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||context.env->ExceptionCheck()||
    !native_source_stop::Session::InspectionAvailable()||
    !deps.driver||!deps.holder||native_source_class_loading::Run::Current()!=deps.loading||
    !deps.loading->OwnsDriver(deps.driver)||!deps.loading->Paused()||
    !deps.execution->Replacements().OwnsSource(*deps.loading,deps.driver,deps.holder)||
    !plan.mirror||!plan.new_classes_root||plan.old_classes.empty()||plan.old_classes.size()!=plan.new_classes.size()||
    (!plan.class_root_origins.empty()&&plan.class_root_origins.size()!=plan.new_classes.size())||
    plan.old_instances.size()!=plan.new_instances.size()||deps.methods.size()!=plan.old_classes.size())
  return deny("call-graph-commit-ordinary-collected-preparation-owners-unavailable");
 std::vector<std::pair<void*,void*>>entries;
 entries.reserve(plan.old_classes.size()+plan.old_instances.size());
 for(size_t i=0;i<plan.old_classes.size();++i){
  if(!plan.old_classes[i]||!plan.new_classes[i]||plan.old_classes[i]==plan.new_classes[i]||!deps.methods[i]||
     !deps.methods[i]->MatchesClassTable(plan.old_classes[i])||!deps.methods[i]->MatchesClassTable(plan.new_classes[i]))
   return deny("call-graph-commit-rooted-class-method-pair-refused");
  void*ext=deps.metadata->Reference(plan.new_classes[i],offsetof(class_metadata::Class11,ext_data));size_t extent=0;
  if(!ext||!deps.read->extent(deps.read->owner,ext,&extent)||extent<sizeof(abi::arm64::ObjectPrefix))
   return deny("call-graph-commit-source-preallocated-ClassExt-unavailable");
  entries.emplace_back(plan.old_classes[i],plan.new_classes[i]);
 }
 for(size_t i=0;i<plan.old_instances.size();++i){
  if(!plan.old_instances[i]||!plan.new_instances[i]||plan.old_instances[i]==plan.new_instances[i])
   return deny("call-graph-commit-rooted-instance-pair-refused");
  entries.emplace_back(plan.old_instances[i],plan.new_instances[i]);
 }
 // The source allocates a distinct unpublished carrier for every predecessor.
 // Sharing a destination would overwrite one predecessor's preserved fields;
 // a destination in the predecessor domain would create a replacement cycle.
 // Build this check before the stop, independently of the map's duplicate-key
 // handling (which only checks predecessors).
 std::vector<uintptr_t>sources,destinations;
 sources.reserve(entries.size());destinations.reserve(entries.size());
 for(const auto&entry:entries){sources.push_back(reinterpret_cast<uintptr_t>(entry.first));destinations.push_back(reinterpret_cast<uintptr_t>(entry.second));}
 std::sort(sources.begin(),sources.end());std::sort(destinations.begin(),destinations.end());
 if(std::adjacent_find(sources.begin(),sources.end())!=sources.end()||
    std::adjacent_find(destinations.begin(),destinations.end())!=destinations.end())
  return deny("call-graph-commit-source-replacement-bijection-refused");
 for(auto destination:destinations)if(std::binary_search(sources.begin(),sources.end(),destination))
  return deny("call-graph-commit-replacement-is-an-original-carrier");
 auto result=std::unique_ptr<Service>(new Service(plan,deps));
 result->plan_storage_={plan.old_classes.data(),plan.new_classes.data(),plan.old_instances.data(),plan.new_instances.data(),plan.class_root_origins.data()};
 result->plan_counts_={plan.old_classes.size(),plan.new_classes.size(),plan.old_instances.size(),plan.new_instances.size(),plan.class_root_origins.size()};
 result->abstract_instances_=plan.abstract_instances;
 result->origin_indices_.reserve(plan.class_root_origins.size());
 for(const auto&origin:plan.class_root_origins){
  if(!origin.array)return deny("call-graph-commit-null-Holder-origin");
  result->origin_indices_.push_back(origin.index);
 }
 result->map_=std::make_unique<object_replacement::ObjectReplacementMap>(entries);
 // The full reflective visitor needs the stopped world. Only build owned
 // native storage here; ValidatePrepared performs that visitor after all
 // root registration/refresh and the genuine collected graph inspection.
 if(result->map_->Size()!=entries.size()||!object_replacement::VerifyObjectReplacementMap(*result->map_,error)||
    !deps.initialization->Validate(plan,deps.read))return nullptr;
 if(!deps.execution->PrepareInterpretation(*deps.interpretation,error)||
    !deps.execution->InterpretationPrepared(*deps.read))return nullptr;
 return result;
}
bool Service::PlanLayoutMatches(const source_commit::Plan&plan)const noexcept{
 if(&plan!=plan_||plan.abstract_instances!=abstract_instances_||!plan.mirror||!plan.new_classes_root||
    plan_storage_!=std::array<const void*,5>{plan.old_classes.data(),plan.new_classes.data(),plan.old_instances.data(),plan.new_instances.data(),plan.class_root_origins.data()}||
    plan_counts_!=std::array<size_t,5>{plan.old_classes.size(),plan.new_classes.size(),plan.old_instances.size(),plan.new_instances.size(),plan.class_root_origins.size()}||
    origin_indices_.size()!=plan.class_root_origins.size())return false;
 for(size_t i=0;i<origin_indices_.size();++i)
  if(plan.class_root_origins[i].index!=origin_indices_[i]||!plan.class_root_origins[i].array)return false;
 return true;
}
bool Service::CollectManagedRoots(const prepared_roots::Registrar&registrar){
 // Registrars may create JNI roots and grow operation storage. Never dispatch
 // them after a source-stop owner has entered its reserved window.
 if(registration_started_||begun_||!PlanLayoutMatches(*plan_)||!native_source_stop::Session::InspectionAvailable()||
    !deps_.loading||native_source_class_loading::Run::Current()!=deps_.loading||
    !deps_.loading->OwnsDriver(deps_.driver)||!deps_.loading->Paused()||
    !map_)return false;
 registration_started_=true;
 if(!rooted_schema_plan::CollectBindings(plan_,registrar,&plan_roots_)||
    !map_->CollectManagedRoots(registrar))return false;
 for(auto*methods:deps_.methods)if(!methods->CollectManagedRoots(registrar))return false;
 registered_=true;return true;
}
bool Service::CanRefresh(const prepared_roots::Resolver&resolver)const noexcept{
 if(!registered_||begun_||!PlanLayoutMatches(*plan_)||!plan_roots_.CanRefresh(resolver)||
    !map_||!map_->CanRefreshManagedRoots(resolver))return false;
 for(auto*methods:deps_.methods)if(!methods->CanRefreshManagedRoots(resolver))return false;
 return true;
}
void Service::Refresh(const prepared_roots::Resolver&resolver)noexcept{
 if(!CanRefresh(resolver))std::abort();final_validated_=false;replacement_roots_={};
 plan_roots_.Refresh(resolver);map_->RefreshManagedRoots(resolver);
 for(auto*methods:deps_.methods)methods->RefreshManagedRoots(resolver);refreshed_=true;
}
bool Service::Window(bool final)const noexcept{
 return deps_.binding&&deps_.binding->runtime_slot_&&*deps_.binding->runtime_slot_==deps_.binding->runtime_&&
  deps_.compilation&&deps_.compilation_receipt&&deps_.compilation->Validate(*deps_.compilation_receipt,deps_.self)&&
  deps_.stop&&(final?deps_.stop->IsFinalWindow(deps_.self):deps_.stop->IsCollectedStop(deps_.self))&&native_source_class_loading::Run::Current()==deps_.loading&&
  deps_.loading->Paused()&&deps_.loading->OwnsDriver(deps_.driver)&&deps_.read&&deps_.read->owner&&deps_.read->extent;
}
bool Service::IsStopped(void*driver,const transaction_root::Holder*holder)const noexcept{
 return driver==deps_.driver&&holder==deps_.holder&&deps_.stop&&deps_.stop->IsStopped(deps_.self);
}
bool Service::IsFinalWindow(void*driver,const transaction_root::Holder*holder)const noexcept{
 return driver==deps_.driver&&holder==deps_.holder&&Window();
}
bool Service::Verify(void*driver,const transaction_root::Holder*holder,const source_commit::Plan&plan)const noexcept{
 return driver==deps_.driver&&holder==deps_.holder&&Validate(plan);
}
bool Service::Validate(const source_commit::Plan&plan)const{
 final_validated_=false;
 final_validated_=ValidateFor(plan,true);return final_validated_;
}
bool Service::ValidatePrepared(const source_commit::Plan&plan)const{
 final_validated_=false;
 return ValidateFor(plan,false);
}
bool Service::ObserveReplacementRoots(bool final)noexcept{
 replacement_roots_={};replacement_roots_final_=final;
 if(!registered_||!refreshed_||begun_||!map_||!Window(final)||!PlanLayoutMatches(*plan_))return false;
 replacement_roots_=native_replacement_root_census::Observe(*map_,*deps_.root_provenance,
   *deps_.stop,deps_.self,*deps_.read);
 return replacement_roots_.completed&&Window(final);
}
bool Service::ValidateFor(const source_commit::Plan&plan,bool final,bool schema_guards)const{
 if(begun_||!registered_||!refreshed_||!Window(final)||!PlanLayoutMatches(plan)||!map_||
    !(final?deps_.execution->ValidateFinalProof(*deps_.stop,*deps_.read):deps_.execution->ValidateCollectedProof(*deps_.stop,*deps_.read))||
    deps_.execution->LastReport().scope!=native_call_graph_execution::GraphScope::InstalledUnionSelection||
    deps_.execution->LastReport().native_boundary_obligations||
    !deps_.execution->InterpretationPrepared(*deps_.read)||
    (schema_guards&&!deps_.schema->VerifyFrozenGuards(plan,*deps_.read))||
    !deps_.initialization->Validate(plan,deps_.read)||
    !deps_.reflection->Validate(*deps_.reflection_plan,nullptr,true)||
    !object_replacement::VerifyObjectReplacementMap(*map_,nullptr)||
    !replacement_roots_.completed||replacement_roots_final_!=final)return false;

 if(map_->Size()!=plan.old_classes.size()+plan.old_instances.size())return false;
 for(size_t i=0;i<plan.old_classes.size();++i){
  if(!map_->Maps(plan.old_classes[i],plan.new_classes[i])||
     !deps_.methods[i]->MatchesClassTable(plan.old_classes[i])||!deps_.methods[i]->MatchesClassTable(plan.new_classes[i]))return false;
 }
 for(size_t i=0;i<plan.old_instances.size();++i)
  if(!map_->Maps(plan.old_instances[i],plan.new_instances[i]))return false;
 return Window(final);
}
void Service::BeginCommit(const source_commit::Plan&plan)const noexcept{
 if(&plan!=plan_||!Window()||replaced_)std::abort();
 // The hierarchy observer and source Services share this exact publication
 // point. They can both forward it; retirement is published only once.
 if(begun_)return;
 if(!final_validated_||!PlanLayoutMatches(plan))std::abort();
 final_validated_=false;begun_=true;deps_.schema->BeginCommit(plan);deps_.reflection->Retire(*deps_.reflection_plan);
}
void Service::RequireCommit()const noexcept{if(!begun_||!Window())std::abort();}
uint32_t Service::Status(void*klass)const{
 const auto&layout=deps_.metadata->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 size_t extent=0;class_metadata::State state{};
 if(!Window()||!deps_.read->extent(deps_.read->owner,klass,&extent)||!class_metadata::ReadState(layout,klass,extent,&state))std::abort();
 return state.status;
}
void Service::SetStatus(void*klass,uint32_t status)const{RequireCommit();if(status>15)std::abort();deps_.binding->status_(klass,uint8_t(status));}
void Service::TransferInitialization(const source_commit::Plan&plan,size_t index)const{
 RequireCommit();if(&plan!=plan_||index>=plan.new_classes.size())std::abort();
 const auto action=deps_.initialization->Transfer(plan,index,deps_.read);
 if(action.action==native_initialized_transfer::Proof::Action::Invalid)std::abort();
 if(action.action==native_initialized_transfer::Proof::Action::CompleteInitialization&&Status(plan.new_classes[index])<action.status)
  SetStatus(plan.new_classes[index],action.status);
}
void Service::CopyFields(bool statics,void*target,void*target_class,void*source,void*source_class)const{
 RequireCommit();deps_.fields->CopyFields(statics,target,target_class,source,source_class,deps_.read);
}
void*Service::ObjectClass(void*object)const{RequireCommit();return deps_.fields->ObjectClass(object);}
void Service::MarkClassAndMethodsObsolete(void*old,void*replacement)const{
 RequireCommit();size_t pair=0;for(;pair<plan_->old_classes.size();++pair)if(plan_->old_classes[pair]==old&&plan_->new_classes[pair]==replacement)break;
 if(pair==deps_.methods.size())std::abort();
 const auto&layout=deps_.metadata->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 auto*flags=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(old)+layout.access_flags);
 __atomic_fetch_or(flags,kObsoleteObject,__ATOMIC_RELAXED);
 structural_methods::Update(*deps_.methods[pair],old,replacement);
}
void Service::UpdateReflectiveReferences(const source_commit::Plan&plan)const{
 RequireCommit();if(&plan!=plan_)std::abort();deps_.reflection->Update(*deps_.reflection_plan,true);
}
void Service::ReplaceObjectReferences()const{
 RequireCommit();if(replaced_||!execution_released_)std::abort();
 weak_replacement_report_=deps_.weak_replacement->ReplaceReferences(*deps_.stop,deps_.self,*map_,deps_.binding->replace_);
 if(!weak_replacement_report_.completed)std::abort();replaced_=true;
}
void Service::ReleaseExecutionEntries()const{
 RequireCommit();if(execution_released_||replaced_)std::abort();
 const auto released=deps_.execution->ReleaseForCommit(*deps_.stop,*deps_.read);
 if(!released.owned_entries_removed)std::abort();execution_released_=true;
}
void Service::RestoreDeclaringClasses(const source_commit::Plan&plan)const{
 RequireCommit();if(&plan!=plan_||!replaced_||restored_)std::abort();
 for(size_t i=0;i<deps_.methods.size();++i){deps_.methods[i]->RestoreOldDeclaringClasses();deps_.fields->RestoreOldFieldDeclaringClass(plan.old_classes[i],deps_.read);}
 restored_=true;
}
void*Service::NewClassAfterReplacement(void*array,size_t index)const{
 RequireCommit();size_t extent=0;int32_t count=0;
 if(!replaced_||!restored_||!array||!deps_.read->extent(deps_.read->owner,array,&extent)||extent<12)std::abort();
 std::memcpy(&count,static_cast<unsigned char*>(array)+8,4);
 if(count<0||index>=size_t(count)||index>(SIZE_MAX-12)/4||12+(index+1)*4>extent)std::abort();
 return deps_.metadata->Reference(array,12+index*4);
}
void Service::RetainOldClass(void*replacement,void*old)const{
 RequireCommit();if(!restored_||!replacement||!old)std::abort();
 void*ext=deps_.metadata->Reference(replacement,offsetof(class_metadata::Class11,ext_data));
 if(!ext)std::abort();deps_.binding->retain_(ext,{reinterpret_cast<uintptr_t>(old)});
}
void Service::ClearInterpreterCaches()const{
 RequireCommit();if(!restored_||cache_cleared_)std::abort();size_t cleared=0;
 if(!deps_.cache->ClearStoppedWorld(&cleared,nullptr)||!cleared)std::abort();cache_cleared_=true;
}
SourceOwner::SourceOwner(const runnable_jni::Context&context,
 const native_source_class_loading::Run&loading,const void*driver,
 native_source_stop::Session&stop,const native_source_compilation::Service&compilation,
 const native_source_compilation::Receipt&receipt)noexcept:loading_(loading),stop_(stop),
 compilation_(compilation),compilation_receipt_(receipt){
 if(!context.self||!context.env||!context.vm||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    !native_source_stop::Session::InspectionAvailable()||stop.Reserved()||
    native_source_class_loading::Run::Current()!=&loading||!loading.OwnsDriver(driver)||
    !compilation_.Validate(compilation_receipt_,context.self))return;
 driver_=driver;self_=context.self;
}
bool SourceOwner::Bind(std::unique_ptr<Service>staged,std::string*error){
 if(!staged)return Fail(error,"call-graph-commit-source-owner-missing-service");
 auto&service=*staged;
 const auto&deps=service.deps_;
 if(service_||!self_||!driver_||deps.self!=self_||deps.driver!=driver_||
    deps.compilation!=&compilation_||deps.compilation_receipt!=&compilation_receipt_||
    !compilation_.Validate(compilation_receipt_,self_)||
    deps.loading!=&loading_||deps.stop!=&stop_||!deps.holder||
    !native_source_stop::Session::InspectionAvailable()||stop_.Reserved()||
    service.registration_started_||service.begun_||!service.PlanLayoutMatches(*service.plan_)||
    !deps.execution->Replacements().OwnsSource(loading_,driver_,deps.holder))
  return Fail(error,"call-graph-commit-source-owner-binding-refused");
 holder_=deps.holder;staged_service_=std::move(staged);service_=staged_service_.get();return true;
}
bool SourceOwner::PreparePublication(const runnable_jni::Context&context,
 native_hierarchy_private_ledger::Ledger&ledger,void*linker,std::string*error){
 if(!SourceMatches(driver_,holder_)||context.self!=self_||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||
    !native_source_stop::Session::InspectionAvailable()||stop_.Reserved()||
    service_->registration_started_||!loading_.Paused())
  return Fail(error,"call-graph-commit-publication-ordinary-owner-refused");
 size_t count=0;
 if(!ledger.PreparePublication(stop_,&count,error))return false;
 native_source_gc_publication::Matcher matcher{&ledger,
  [](void*owner,void*driver,const void*holder,void*class_linker,void*cache,
     void*loader,size_t*index)noexcept{
   return static_cast<native_hierarchy_private_ledger::Ledger*>(owner)->MatchPublication(
    driver,holder,class_linker,cache,loader,index);
  }};
 if(!stop_.PrepareDexPublication(const_cast<void*>(driver_),holder_,linker,count,matcher,error))return false;
 publication_linker_=linker;return true;
}
bool SourceOwner::SourceMatches(const void*driver,const void*holder)const noexcept{
 return service_&&self_&&driver==driver_&&holder&&holder==holder_&&
  native_source_class_loading::Run::Current()==&loading_&&loading_.OwnsDriver(driver_);
}
bool SourceOwner::RegisterRoots(const runnable_jni::Context&context,
 prepared_roots::Registry&registry,native_hierarchy_graph::Authority&authority,
 native_hierarchy_private_ledger::Ledger&ledger,std::string*error){
 auto*hierarchy=native_hierarchy_session::Session::Current();
 if(root_registry_||!SourceMatches(driver_,holder_)||context.self!=self_||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||!loading_.Paused()||
    !native_source_stop::Session::InspectionAvailable()||stop_.Reserved()||
    !hierarchy||!hierarchy->OwnsDriver(driver_)||!hierarchy->OwnsAuthority(authority)||
    !registry.NativeInsertionReady()||service_->registration_started_)
  return Fail(error,"call-graph-commit-root-registration-owner-refused");
 // Registration is single-attempt, including partial failure. All participants
 // address already staged containers; retrying a partial registrar would mix
 // frozen pointer slots with a different root insertion cohort.
 root_registry_=&registry;authority_=&authority;ledger_=&ledger;
 const auto registrar=registry.Collect();
 if(!service_->CollectManagedRoots(registrar)||
    !service_->deps_.reflection_plan->CollectManagedRoots(registrar)||
    !authority.CollectManagedRoots(registrar)||!ledger.CollectManagedRoots(registrar))
  return Fail(error,"call-graph-commit-participant-root-registration-refused");
 if(!service_->deps_.schema->CollectManagedRoots(registrar,error))return false;
 return registry.Freeze()||Fail(error,"call-graph-commit-root-registry-freeze-refused");
}
bool SourceOwner::RefreshRoots(const char**refusal)noexcept{
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if(refusal)*refusal=nullptr;
 if(!SourceMatches(driver_,holder_)||!stop_.IsStopped(self_)||
    !root_registry_||!authority_||!ledger_)
  return deny("call-graph-commit-stopped-root-registry-refused");
 if(!root_registry_->Resolve()){
  InvalidateRoots();return deny("call-graph-commit-root-registry-resolution-refused");
 }
 const auto resolver=root_registry_->Final();
 const auto&deps=service_->deps_;
 if(!service_->CanRefresh(resolver)||!deps.reflection_plan->CanRefreshManagedRoots(resolver)||
    !authority_->CanRefresh(resolver)||!ledger_->CanRefresh(resolver)||
    !deps.schema->CanRefreshManagedRoots(resolver)){
  InvalidateRoots();return deny("call-graph-commit-participant-root-refresh-refused");
 }
 service_->Refresh(resolver);deps.reflection_plan->RefreshManagedRoots(resolver);
 authority_->Refresh(resolver);ledger_->Refresh(resolver);
 // Schema refresh compares the refreshed exact source Plan and independently
 // frozen public census. Failure keeps the reservation rejectable in Collected;
 // a final caller treats it as an irreversible source invariant violation.
 if(!deps.schema->RefreshManagedRoots(*service_->plan_,resolver)){
  InvalidateRoots();return deny("call-graph-commit-schema-root-refresh-refused");
 }
 authority_->UseReadContext(*deps.read);
 if(!deps.schema->UseStoppedReadContext(deps.read)){
  InvalidateRoots();return deny("call-graph-commit-schema-stopped-reader-refused");
 }
 return true;
}
bool SourceOwner::Collected(native_hierarchy_session::Session&hierarchy,const char**refusal)noexcept{
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if(refusal)*refusal=nullptr;
 if(!SourceMatches(driver_,holder_)||!stop_.IsCollectedStop(self_)||
    native_hierarchy_session::Session::Current()!=&hierarchy||!hierarchy.Ready()||
    !hierarchy.OwnsDriver(driver_)||!service_->registered_||
    !service_->PlanLayoutMatches(*service_->plan_))
  return deny("call-graph-commit-collected-source-owner-refused");
 if(!RefreshRoots(refusal))return false;
 const auto&deps=service_->deps_;
 const auto report=deps.execution->ValidateCollected(stop_,loading_,driver_,*deps.read);
 if(report.status!=native_call_graph_execution::Status::CollectedDrained||
    report.scope!=native_call_graph_execution::GraphScope::InstalledUnionSelection||!report.union_complete)
  return deny(report.failure?report.failure:"call-graph-commit-collected-execution-refused");
 const auto*frames=deps.execution->ValidatedFrames(stop_,*deps.read);
 if(!frames||!deps.schema->VerifyFrozenBoundaryMembers(*service_->plan_,*frames,*deps.read))
  return deny("call-graph-commit-collected-boundary-member-census-refused");
 const auto roots=deps.execution->ObserveFrameRoots(stop_,*deps.read,*deps.root_provenance);
 if(!roots.completed)return deny(roots.failure?roots.failure:"call-graph-commit-collected-frame-root-stream-refused");
 if(!service_->ObserveReplacementRoots(false))
  return deny("call-graph-commit-collected-replacement-root-census-refused");
 if(!hierarchy.Collected(*service_->plan_,nullptr))
  return deny("call-graph-commit-collected-hierarchy-seal-refused");
 // Hierarchy requests require the sealed, independently refreshed authority.
 // Install its actual source adapter before consuming the complete guard set.
 if(!deps.schema->UseHierarchyAuthority(this))
  return deny("call-graph-commit-collected-schema-authority-refused");
 if(!service_->ValidatePrepared(*service_->plan_))
  return deny("call-graph-commit-collected-operands-refused");
 return true;
}
bool SourceOwner::VerifyHierarchy(const source_commit::Plan&plan,size_t pair,
 void*loader)const noexcept {
 if(!SourceMatches(driver_,holder_)||!service_||!authority_||!root_registry_||
    !service_->registered_||!service_->refreshed_||service_->begun_||
    &plan!=service_->plan_||pair>=plan.old_classes.size()||
    !service_->PlanLayoutMatches(plan)||!stop_.IsStopped(self_))return false;
 auto*hierarchy=native_hierarchy_session::Session::Current();
 if(!hierarchy||!hierarchy->Ready()||!hierarchy->OwnsDriver(driver_)||
    !hierarchy->OwnsAuthority(*authority_))return false;
 field_copy::CopyClassView identity{};
 const auto&deps=service_->deps_;
 if(!deps.metadata->ClassIdentity(plan.old_classes[pair],&identity,nullptr,deps.read)||
    identity.loader!=loader)return false;
 if(stop_.IsFinalWindow(self_))return authority_->VerifyFinal(plan,pair);
 return stop_.IsCollectedStop(self_)&&authority_->VerifyPrepared(plan,pair);
}
bool SourceOwner::ContinueCollected(const runnable_jni::Context&context,
 native_hierarchy_session::Session&hierarchy,size_t extent,const char**refusal)noexcept{
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if(refusal)*refusal=nullptr;
 if(!SourceMatches(driver_,holder_)||context.self!=self_||!extent||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||
    native_hierarchy_session::Session::Current()!=&hierarchy||!hierarchy.OwnsDriver(driver_)||
    !hierarchy.Ready()||!native_source_stop::Session::InspectionAvailable()||
    !root_registry_||!authority_||!ledger_||!service_->registered_||
    !stop_.PublicationPrepared(const_cast<void*>(driver_),holder_,publication_linker_))
  return deny("call-graph-commit-collected-continuation-owner-refused");
 struct Request{SourceOwner&owner;native_hierarchy_session::Session&hierarchy;const char*failure=nullptr;};
 Request request{*this,hierarchy};
 const auto prepare=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&r=*static_cast<Request*>(raw);
  if(&stop!=&r.owner.stop_)std::abort();
  if(!r.owner.Collected(r.hierarchy,&r.failure))return false;
  if(!stop.ContinueSource()){r.failure=stop.Failure();return false;}
  return true;
 };
 if(!stop_.BeginPrepared(self_,extent,&request,prepare)){
  InvalidateRoots();
  return deny(request.failure?request.failure:stop_.Failure());
 }
 // Source publication follows this accepted callback. From this point no
 // ordinary refund can unwind the transferred global reservation.
 if(!stop_.CanPublishDex(const_cast<void*>(driver_),holder_,publication_linker_))std::abort();
 return true;
}
void SourceOwner::Finalize(void*driver,const transaction_root::Holder*holder)noexcept{
 if(!SourceMatches(driver,holder)||!stop_.IsFinalWindow(self_)||service_->begun_||
    !service_->registered_||!service_->PlanLayoutMatches(*service_->plan_)||!RefreshRoots()||
    !authority_)std::abort();
 const auto&deps=service_->deps_;
 const auto report=deps.execution->ValidateFinal(stop_,loading_,driver,*deps.read);
 if(report.status!=native_call_graph_execution::Status::FinalDrained||
    report.scope!=native_call_graph_execution::GraphScope::InstalledUnionSelection||
    !report.union_complete)std::abort();
 const auto*frames=deps.execution->ValidatedFrames(stop_,*deps.read);
 if(!frames||!deps.schema->VerifyFrozenBoundaryMembers(*service_->plan_,*frames,*deps.read))std::abort();
 if(!deps.execution->ObserveFrameRoots(stop_,*deps.read,*deps.root_provenance).completed||
    !service_->ObserveReplacementRoots(true)||
    !authority_->VerifyFinal(*service_->plan_,0)||
    !service_->Validate(*service_->plan_))std::abort();
}
std::unique_ptr<source_commit::Services>SourceOwner::TakeFinalService(const void*holder,
 const source_commit::Plan**plan)noexcept{
 if(!plan||!staged_service_||staged_service_.get()!=service_||!SourceMatches(driver_,holder))std::abort();
 *plan=nullptr;
 Finalize(const_cast<void*>(driver_),static_cast<const transaction_root::Holder*>(holder));
 *plan=service_->plan_;return std::move(staged_service_);
}
void SourceOwner::Commit(const source_commit::Plan&plan)noexcept{
 auto*hierarchy=native_hierarchy_session::Session::Current();
 if(staged_service_||!SourceMatches(driver_,holder_)||!stop_.IsFinalWindow(self_)||
    &plan!=service_->plan_||!hierarchy||!hierarchy->Ready()||!hierarchy->OwnsDriver(driver_))std::abort();
 // The sole NotifyCommit observer calls this before source Services starts
 // mutation. Authority/ledger forwarding publishes the same service once.
 hierarchy->BeginCommit(plan);
}
void SourceOwner::ReleaseAfterSuccess(void*driver)noexcept{
 // The source callback still owns the loading observer and Request still
 // owns Service. Only completed native lifecycles and journals are read here;
 // its borrowed plan and obsolete Class roots are already out of scope.
 if(staged_service_||!service_||!self_||driver!=driver_||
    native_source_class_loading::Run::Current()!=&loading_||!loading_.OwnsDriver(driver)||
    !loading_.CleanupCompleted()||!stop_.Completed()||stop_.Reserved()||
    !service_->begun_||!service_->execution_released_||!service_->replaced_||
    !service_->restored_||!service_->cache_cleared_||
    !service_->deps_.execution->CommittedEntriesReleased())std::abort();
 Close();
}
void SourceOwner::ReleaseAfterFailure(void*driver,
 const native_hierarchy_session::Session&hierarchy)noexcept{
 // The hierarchy owner already refunded entries, discarded captured private
 // objects and released the ledger before issuing this scalar receipt. The
 // original Holder is gone; do not try to repair missing cleanup here.
 if(!self_||driver!=driver_||!references_dropped_||!hierarchy.ReleasedAfterFailure(driver)||
    native_source_class_loading::Run::Current()!=&loading_||!loading_.OwnsDriver(driver)||
    stop_.Reserved()||stop_.IsStopped(self_)||
    (service_&&(service_!=staged_service_.get()||service_->begun_)))std::abort();
 Close();
}
bool SourceOwner::DropPreparedReferences(void*driver)noexcept{
 if(references_dropped_||!self_||driver!=driver_||native_source_class_loading::Run::Current()!=&loading_||
    !loading_.OwnsDriver(driver_)||stop_.Reserved()||stop_.IsStopped(self_)||
    (service_&&(service_!=staged_service_.get()||service_->begun_)))return false;
 InvalidateRoots();
 if(root_registry_)root_registry_->Clear();
 references_dropped_=true;
 return true;
}
void SourceOwner::InvalidateRoots()noexcept{
 if(root_registry_)root_registry_->Invalidate();
 if(ledger_)ledger_->InvalidateRoots();
 if(service_){service_->refreshed_=false;service_->final_validated_=false;service_->replacement_roots_={};}
}
void SourceOwner::Close()noexcept{
 InvalidateRoots();
 holder_=nullptr;service_=nullptr;driver_=nullptr;self_=nullptr;
 root_registry_=nullptr;authority_=nullptr;ledger_=nullptr;publication_linker_=nullptr;
 // Destruction reads no method/Class/DEX or registered pointer location.
 // The source and registry retain ownership of their separate roots.
 staged_service_.reset();
}
bool SourceOwner::IsStopped(void*driver,const transaction_root::Holder*holder)const noexcept{
 // Ledger's first root refresh precedes graph/schema validation. The real
 // stop witness, not those later receipts, authorizes this bounded root read.
 return SourceMatches(driver,holder)&&stop_.IsStopped(self_);
}
bool SourceOwner::IsFinalWindow(void*driver,const transaction_root::Holder*holder)const noexcept{
 return SourceMatches(driver,holder)&&service_->IsFinalWindow(driver,holder);
}
bool SourceOwner::Verify(void*driver,const transaction_root::Holder*holder,
 const source_commit::Plan&plan)const noexcept{
 if(!SourceMatches(driver,holder))return false;
 // This is the ledger's independent cleanup/operand proof, which is one
 // constituent of hierarchy authority. Calling the schema hierarchy guard
 // here would recurse through Authority -> Ledger -> Cleanup -> Authority.
 // The outer Validate/ValidatePrepared consumes all schema guards only after
 // that authority has passed; only Validate can mint the final commit receipt.
 if(stop_.IsFinalWindow(self_))return service_->ValidateFor(plan,true,false);
 return stop_.IsCollectedStop(self_)&&service_->ValidateFor(plan,false,false);
}
void SourceOwner::BeginCommit(const source_commit::Plan&plan)const noexcept{
 if(!SourceMatches(driver_,holder_)||!stop_.IsFinalWindow(self_))std::abort();
 service_->BeginCommit(plan);
}
}

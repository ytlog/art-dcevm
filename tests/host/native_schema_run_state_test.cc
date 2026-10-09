#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#include "pipeline_fixture.inc"
#include "cleanup_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_schema_run_state.h"
#include <sys/wait.h>
#include <unistd.h>
template<class F> void ExpectAbort(F fn){const auto child=fork();assert(child>=0);if(!child){fn();_exit(1);}int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
int main(){
#include "root_setup.inc"
#include "pipeline_setup.inc"
  // Additional actual owners required by the aggregate. Calibrate all providers
  // ordinary; no injectable RunState readiness shortcuts are used.
  OwnerHooks owner_hooks;native_cache::Service process_cache;OwnerNative(thread.data());assert(process_cache.Bind(owner_hooks,&thread_list,&error)&&process_cache.Prepare(&error));OwnerRunnable(thread.data());
  table.GetJavaVM=PublicVM;
  native_structural_reflection::Service reflection;
  method_identity::Calibration method_model;
  Methods(old_class,false);Methods(new_class,false);
  std::array<method_identity::Oracle,3> method_oracles{};
  for(size_t i=0;i<3;++i){auto*m=reinterpret_cast<abi::arm64::android12::Method12*>(reinterpret_cast<char*>(old_method)+i*32);executable[i]=new(heap.New(sizeof(executable_mirror::Declaration)))executable_mirror::Declaration{};executable[i]->art_method=uintptr_t(m);executable[i]->dex_method_index=i;executable[i]->access_flags=i==0?0x101:i==1?1:9;method_oracles[i]={reinterpret_cast<jobject>(executable[i]),uint32_t(i),executable[i]->access_flags};}
  std::unique_ptr<executable_mirror::Layout> method_layout;std::unique_ptr<modern_methods::Binding> methods;
  assert(method_identity::Calibrate(context,Leaf,method_oracles.data(),3,&method_model,&error));
  method_layout=executable_mirror::Layout::Probe(context,Leaf,DecodeMirror,method_oracles.data(),3,&error);assert(method_layout);
  methods=modern_methods::Service::BindOwned(owner_hooks,context,Leaf,method_oracles[0],&binding_value,&linker,method_model.model,&error);assert(methods);
  assert(reflection.Bind(owner_hooks,&::runtime,*method_layout,&error));
  native_member_provider::Dependencies members;members.hooks=&owner_hooks;members.methods=methods.get();members.method_model=&method_model;members.metadata=d.metadata;members.fields=d.storage;members.cache=&process_cache;members.reflection=&reflection;members.is_retired=OwnerRetired;members.allow_field_removal=true;members.retirement={nullptr,NotRetired,NotRetired,OwnerRetire,RetireField};members.handle_guards={nullptr,Mh,Vh};
  members.runtime.runtime=&::runtime;members.runtime.instrumentation=&instrumentation;members.runtime.receivers.java_vm=reinterpret_cast<JavaVM*>(&::runtime);members.runtime.receivers.class_linker=&linker;members.runtime.receivers.thread_list=&thread_list;members.runtime.receivers.jit=&jit;members.runtime.receivers.jit_code_cache=&jit_cache;
  actual_driver.runtime=&::runtime;
  native_required_cleanup::Binding closure;assert(native_required_cleanup::Binding::Bind(owner_hooks,&env,members,&closure,&error));
  OwnerNative(thread.data());assert(native_source_stop::Install(owner_hooks,&error));OwnerRunnable(thread.data());
  thread_suspend_service::Service states;assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char*n){return owner_hooks.FindSymbol("libart.so",n);},&states));
  native_source_stop::Service source;assert(native_source_stop::Service::Bind(owner_hooks,&owner_heap,states,&source,&error));
  int original_run;
  {native_source_stop::Observation observation(source,&actual_driver,&original_run,thread.data(),thread.size());assert(observation.Collected(&actual_driver,&holder));owner_disable_proxy(&owner_heap,thread.data());OwnerNative(thread.data());unsigned char carrier;owner_stop_proxy(&carrier,s::kFinalCause,true);owner_resume_proxy(&carrier);OwnerRunnable(thread.data());owner_enable_proxy(&owner_heap,thread.data());native_source_stop::Calibration proof;assert(observation.Finish(&actual_driver,true,&proof)&&source.Authorize(proof,&original_run));}
  whole_process_frames::Service scanner;assert(whole_process_frames::Service::BindGeometryOnly(code_info_view::SourceFamily::kAndroid13,[&](const char*n){return owner_hooks.FindSymbol("libart.so",n);},&scanner));
  prepared_roots::Registry roots({nullptr,Register,Resolve,Release});native_member_provider::Service provider;
  native_schema_run_state::Dependencies deps;deps.hooks=&owner_hooks;deps.loading=&class_loading;deps.stop=&source;deps.roots=d.roots;deps.allocator=&allocation;deps.dex_registry=&registry;deps.lookup=&hashed_lookup;deps.frames=&scanner;deps.cleanup=&closure;deps.provider=&provider;deps.members=members;deps.registry=&roots;
  native_schema_run_state::CacheCalibration witness{reinterpret_cast<jobject>(published_cache),reinterpret_cast<jobject>(old_class),112,counts,subject_index,0,0,old_fields[0],old_method};
  CheckHooks checks;assert(native_hierarchy_source_checks::Install(checks,&error));native_hierarchy_source_checks::SetFailureObserver(native_owned_removal::ObserveSourceFailure);
  auto aggregate_context=context;aggregate_context.vm=reinterpret_cast<JavaVM*>(&::runtime);
  // We changed the standalone method-array pointer only after the old type cache
  // was populated; refresh that independent witness explicitly before Begin.
  auto* published=static_cast<type_cache::Dual*>(published_cache);reinterpret_cast<method_cache::Pair*>(published->methods)[0].method=uintptr_t(old_method);reinterpret_cast<std::atomic<uintptr_t>*>(published->methods_array)[0]=uintptr_t(old_method);
  void* app_loader=heap.New(32);for(const auto& klass:public_classes)if(klass.native!=parent_class)Put32(klass.native,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(app_loader)));Put32(new_class,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(app_loader)));
  auto begin=[&](const native_schema_run_state::CacheCalibration& w){return native_schema_run_state::RunState::Begin(deps,&actual_driver,aggregate_context,&ti,{proposal},w,&error);};
  auto missing=witness;missing.strong_cache=nullptr;assert(!begin(missing));assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());
  for(int repeat=0;repeat<2;++repeat){auto run=begin(witness);if(!run){std::cerr<<"aggregate Begin: "<<error<<"\n";std::abort();}assert(native_schema_run_state::RunState::Current()==run.get());assert(!begin(witness));ExpectAbort([&]{run->End(&actual_driver,false);});ExpectAbort([&]{source_commit::Plan wrong;run->Commit(wrong);});assert(!run->StoppedWindow());source_commit::Plan not_sealed;assert(!run->VerifyHierarchy(not_sealed,0,nullptr));transaction_root::Iterator unallocated{0,&holder};assert(!run->BeforeVerification(&actual_owner,&unallocated,&error));run->AfterVerification(&actual_owner,&unallocated,false);source_commit::Plan uncollected;assert(!run->Collected({},uncollected,&error));const source_commit::Plan* ignored=nullptr;const auto os_queries=method_identity::MappingOsQueries();forbid_allocation=true;assert(!run->Finalize(&holder,&ignored));assert(method_identity::MappingOsQueries()==os_queries);forbid_allocation=false;run->Refund();run->Refund();assert(run->End(&actual_driver,false));run.reset();assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());}
  {
   auto run=begin(witness);assert(run);
   PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);
   selected_parent=inserted;session_old_handle=old_root;session_parent_handle=parent_root;session_cache_handle=cache_root;
   transaction_root::Iterator iter{0,&holder};
   if(!session_finish_proxy(&actual_owner,&holder,&iter)){std::cerr<<"actual aggregate Finish: "<<error<<" result="<<actual_driver.result<<" failure="<<actual_failure_message<<"\n";std::abort();}
   assert(session_allocated&&allocated==2&&relocated_new&&session_allocated==relocated_new);
   assert(run->BeforeVerification(&actual_owner,&iter,&error));run->AfterVerification(&actual_owner,&iter,true);
   // The actual native provider rejects this intentionally different installed
   // DEX/member declaration set after the aggregate acquires the real stop.
   source_commit::Plan incomplete;incomplete.mirror=old_class;incomplete.old_classes={old_class};incomplete.new_classes={session_allocated};
   auto* collected=heap.New(16);Put32(collected,0,uint32_t(uintptr_t(array_type)));Put32(collected,8,1);Put32(collected,12,uint32_t(uintptr_t(session_allocated)));incomplete.new_classes_root=collected;incomplete.class_root_origins={{collected,0}};Put32(array,12+11*4,uint32_t(uintptr_t(collected)));
   native_owned_removal::RootedGraph graph;graph.holder=&holder;assert(transaction_root::ReadMetadata(holder,1,transaction_root::OwnerFamily::kAndroid15,&graph.metadata));
   assert(!run->Collected(graph,incomplete,&error));std::cerr<<"actual aggregate Collected refusal: "<<error<<" stopped="<<run->StoppedWindow()<<"\n";
   assert(run->StoppedWindow()&&gc&&!disabled&&stopped==1&&roots.Size()>0);
   void* actual_loader=d.metadata->Reference(incomplete.old_classes[0],offsetof(class_metadata::Class11,class_loader));
   assert(run->VerifyHierarchy(incomplete,0,actual_loader));
   auto reused_plan=incomplete;assert(!run->VerifyHierarchy(reused_plan,0,actual_loader));
   assert(!run->VerifyHierarchy(incomplete,0,reinterpret_cast<void*>(uintptr_t(actual_loader)+8)));
   assert(!run->VerifyHierarchy(incomplete,1,actual_loader));
   run->Refund();assert(!run->VerifyHierarchy(incomplete,0,actual_loader));assert(!gc&&!disabled&&!stopped&&Read32(thread.data())==0);assert(Read32(session_allocated,offsetof(class_metadata::Class17,status))>>28==1);
   class_remove_proxy(&linker,&pauser);assert(run->End(&actual_driver,false));run.reset();roots.Clear();assert(roots.Size()==0);
   assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());
  }
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);
 std::cout<<"Actual RunState aggregate: failed Begin retry, repeated refund/End, early Commit/End abort guards, wrong-phase Finalize noalloc/noOS, actual Finish+GCmove+verification+Collected stopped refusal+rollback+retirement+End; ART/JNI leaf fixtures only\n";
}

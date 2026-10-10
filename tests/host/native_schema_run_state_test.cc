#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#include "pipeline_fixture.inc"
#include "cleanup_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_schema_run_state.h"
#include "native_schema_run_state_mapping_fixture.h"
#include "native_schema_run_state_typed_fixture.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_schema_run_state_public_fixture.h"
#include "native_schema_run_state_cache_roles_fixture.h"
#pragma clang diagnostic pop
#include <sys/wait.h>
#include <unistd.h>
template<class F> void ExpectAbort(F fn){const auto child=fork();assert(child>=0);if(!child){fn();_exit(1);}int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
// The production refund still aborts on foreign/published ownership; capture
// its literal diagnostic without substituting any Ledger/Batch permission.
template<class F> void ExpectRefundRefusal(F fn,const char* expected){
 int descriptors[2];assert(pipe(descriptors)==0);const auto child=fork();assert(child>=0);
 if(!child){close(descriptors[0]);assert(dup2(descriptors[1],STDERR_FILENO)>=0);close(descriptors[1]);fn();_exit(1);}
 close(descriptors[1]);char bytes[1024]{};const auto count=read(descriptors[0],bytes,sizeof(bytes)-1);close(descriptors[0]);
 int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(count>0&&std::strstr(bytes,expected));
}
int main(){
#include "root_setup.inc"
#include "pipeline_setup.inc"
  // Compose only the newly consumed JNI leaves; preserve graph/provider negative inputs.
  table.CallStaticBooleanMethodA=GuardProxy;table.NewObjectArray=GuardNewArray;table.GetArrayLength=GuardLength;table.GetObjectArrayElement=GuardGet;table.SetObjectArrayElement=GuardSet;
  std::vector<unsigned char> graph_bytes=File(RUN_STATE_GRAPH_DEX_FIXTURE);
  PrepareSchemaTypedObjects();ConfigureDefiningGraph(graph_bytes,*d.metadata);
  // Additional actual owners required by the aggregate. Calibrate all providers
  // ordinary; no injectable RunState readiness shortcuts are used.
  PrepareSchemaTypedObjects();OwnerHooks owner_hooks;native_cache::Service process_cache;OwnerNative(thread.data());assert(process_cache.Bind(owner_hooks,&thread_list,&error)&&process_cache.Prepare(&error));OwnerRunnable(thread.data());
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
  void* app_loader=heap.New(32);for(const auto& klass:public_classes)if(klass.native!=parent_class)Put32(klass.native,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(app_loader)));Put32(new_class,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(app_loader)));
  {native_source_stop::Observation observation(source,&actual_driver,&original_run,thread.data(),thread.size());assert(observation.Collected(&actual_driver,&holder));
   // Observe the real unique named registration wrapper inside this exact
   // ordinary source Run. This fixture original has zero nested GC pairs;
   // one-pair originals and all malformed scopes are tested by source-stop.
   publication_proxy(&linker,{uintptr_t(cache)},{uintptr_t(app_loader)});
   owner_disable_proxy(&owner_heap,thread.data());OwnerNative(thread.data());unsigned char carrier;owner_stop_proxy(&carrier,s::kFinalCause,true);owner_resume_proxy(&carrier);OwnerRunnable(thread.data());owner_enable_proxy(&owner_heap,thread.data());native_source_stop::Calibration proof;assert(observation.Finish(&actual_driver,true,&proof)&&source.Authorize(proof,&original_run));}
  whole_process_frames::Service scanner;assert(whole_process_frames::Service::BindGeometryOnly(code_info_view::SourceFamily::kAndroid13,[&](const char*n){return owner_hooks.FindSymbol("libart.so",n);},&scanner));
  prepared_roots::Registry roots({nullptr,Register,Resolve,Release});native_member_provider::Service provider;
  native_schema_run_state::Dependencies deps;deps.hooks=&owner_hooks;deps.loading=&class_loading;deps.stop=&source;deps.roots=d.roots;deps.allocator=&allocation;deps.dex_registry=&registry;deps.lookup=lookup_service;deps.frames=&scanner;deps.cleanup=&closure;deps.provider=&provider;deps.members=members;deps.registry=&roots;
  size_t released_failed_roots=0;deps.refund_roots_owner=&released_failed_roots;
  deps.refund_roots=[](void*raw)noexcept{++*static_cast<size_t*>(raw);};
  RunStateMappings mappings;deps.mapping={&mappings,RunStateMappings::Refresh,RunStateMappings::Read};
  native_typed_heap_walk::Service typed_heap;CalibrateSchemaTypedHeap(owner_hooks,typed_heap,*d.metadata,funcs,error);deps.typed_heap=&typed_heap;
  PreparedCacheRoles cache_role_fixture;
  native_schema_run_state::CacheCalibration witness{nullptr,reinterpret_cast<jobject>(published_cache),reinterpret_cast<jobject>(old_class),112,counts,subject_index,0,0,old_fields[0],old_method};
  CheckHooks checks;assert(native_hierarchy_source_checks::Install(checks,&error));native_hierarchy_source_checks::SetFailureObserver(native_owned_removal::ObserveSourceFailure);
  auto aggregate_context=context;aggregate_context.vm=reinterpret_cast<JavaVM*>(&::runtime);
  // We changed the standalone method-array pointer only after the old type cache
  // was populated; refresh that independent witness explicitly before Begin.
  auto* published=static_cast<type_cache::Dual*>(published_cache);reinterpret_cast<method_cache::Pair*>(published->methods)[0].method=uintptr_t(old_method);reinterpret_cast<std::atomic<uintptr_t>*>(published->methods_array)[0]=uintptr_t(old_method);

  struct PublicStorageState {type_cache::Dual* cache;bool resolved=false,accept=false;size_t reads=0;} public_state{published};
  witness.resolution_owner=&public_state;
  witness.resolve=[](void*raw,const runnable_jni::Context&,source_handle_roots::Pool&,void*,void*,std::string*){static_cast<PublicStorageState*>(raw)->resolved=true;return true;};
  witness.public_storage=[](void*raw,const runnable_jni::Context& c,native_hierarchy_cache::Witness*out,std::string*){auto& p=*static_cast<PublicStorageState*>(raw);assert(p.resolved&&c.env&&!stopped);++p.reads;if(!p.accept)return false;out->has_public_storage=true;out->public_types=p.cache->types;out->public_fields=p.cache->fields;out->public_methods=p.cache->methods;return true;};
  size_t typed_globals_before_begin=SchemaTypedGlobals();
  if(!cache_role_fixture.Prepare(File(RUN_STATE_CACHE_DEX_FIXTURE),&env,&ti,*d.metadata,*d.roots,&error)){std::cerr<<"aggregate Public cache roles: "<<error<<"\n";std::abort();}
  cache_role_fixture.Witness(&witness);typed_globals_before_begin=SchemaTypedGlobals();
  auto begin=[&](const native_schema_run_state::CacheCalibration& w){return native_schema_run_state::RunState::Begin(deps,&actual_driver,aggregate_context,&ti,{proposal},w,&error);};
  for(int missing_map=0;missing_map<3;++missing_map){auto complete=deps.mapping;if(missing_map==0)deps.mapping.owner=nullptr;else if(missing_map==1)deps.mapping.refresh_stopped=nullptr;else deps.mapping.extent=nullptr;assert(!begin(witness));assert(!native_schema_run_state::RunState::Current());deps.mapping=complete;}
  {auto rejected=begin(witness);if(rejected||!public_state.resolved||public_state.reads!=1){std::cerr<<"first aggregate Begin rejection: "<<error<<" resolved="<<public_state.resolved<<" reads="<<public_state.reads<<"\n";std::abort();}}assert(!native_schema_run_state::RunState::Current());assert(SchemaTypedGlobals()==typed_globals_before_begin);public_state.accept=true;
  auto missing=witness;missing.strong_cache=nullptr;assert(!begin(missing));assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());
  {const auto original=proposal.strong_original;proposal.strong_original=nullptr;assert(!begin(witness));proposal.strong_original=original;assert(!native_schema_run_state::RunState::Current());assert(SchemaTypedGlobals()==typed_globals_before_begin);}
  for(int repeat=0;repeat<2;++repeat){auto run=begin(witness);if(!run){std::cerr<<"aggregate Begin: "<<error<<"\n";std::abort();}assert(native_schema_run_state::RunState::Current()==run.get());assert(!begin(witness));ExpectAbort([&]{run->End(&actual_driver,false);});ExpectAbort([&]{source_commit::Plan wrong;run->Commit(wrong);});assert(!run->StoppedWindow());source_commit::Plan not_sealed;assert(!run->VerifyHierarchy(not_sealed,0,nullptr));transaction_root::Iterator unallocated{0,&holder};assert(!run->BeforeVerification(&actual_owner,&unallocated,&error));run->AfterVerification(&actual_owner,&unallocated,false);source_commit::Plan uncollected;assert(!run->Collected({},uncollected,&error));const source_commit::Plan* ignored=nullptr;const auto os_queries=method_identity::MappingOsQueries();forbid_allocation=true;assert(!run->Finalize(&holder,&ignored));assert(method_identity::MappingOsQueries()==os_queries);forbid_allocation=false;run->Refund();run->Refund();assert(run->End(&actual_driver,false));run.reset();assert(SchemaTypedGlobals()==typed_globals_before_begin);assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());}
  {
   auto run=begin(witness);assert(run);
   PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);
   selected_parent=inserted;session_old_handle=old_root;session_parent_handle=parent_root;session_cache_handle=cache_root;
   transaction_root::Iterator iter{0,&holder};
   if(!session_finish_proxy(&actual_owner,&holder,&iter)){std::cerr<<"actual aggregate Finish: "<<error<<" result="<<actual_driver.result<<" failure="<<actual_failure_message<<"\n";std::abort();}
   assert(session_allocated&&allocated==2&&relocated_new&&session_allocated==relocated_new);
   // Actual source ForceClassInitialized leaves the private replacement at 14.
   // Failed ordinary/stopped preparation must not attempt its forbidden 14->1
   // transition; original Holder still owns it until original Run unwinds.
   Put32(session_allocated,offsetof(class_metadata::Class17,status),14u<<28);
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
   // Exact live source queries remain necessary even on a failed plan: neither
   // a published DEX nor a source owner transferred to another driver may be
   // silently dropped. Child processes preserve the parent's full root set.
   ExpectRefundRefusal([&]{registry_published=true;run->Refund();},"private-discard-source-DexFile-already-registered");
   ExpectRefundRefusal([&]{Driver foreign=actual_driver;actual_owner.driver=&foreign;run->Refund();},"private-discard-source-definition-driver-differs");
   ExpectRefundRefusal([&]{Put32(session_allocated,offsetof(class_metadata::Class11,dex_cache),0);run->Refund();},"private-temporary-Class-cache-differs");
   run->Refund();assert(!run->VerifyHierarchy(incomplete,0,actual_loader));assert(!gc&&!disabled&&!stopped&&Read32(thread.data())==0);assert(Read32(session_allocated,offsetof(class_metadata::Class17,status))>>28==14);
   assert(roots.Size()==0); // Registry released before source Holder/DEX exit.
   assert(Read32(collected,12)==uint32_t(uintptr_t(session_allocated))); // Original root intact.
   class_remove_proxy(&linker,&pauser);
   const auto destroyed_holder=holder;std::memset(&holder,0,sizeof(holder)); // Original Run already destroyed Holder.
   assert(run->End(&actual_driver,false));run.reset();assert(SchemaTypedGlobals()==typed_globals_before_begin);holder=destroyed_holder;roots.Clear();assert(roots.Size()==0);
   assert(!native_schema_run_state::RunState::Current()&&!native_hierarchy_session::Session::Current()&&!native_source_class_loading::Run::Current());
  }
  {
   Put32(array,12+11*4,0);Put32(array,12+12*4,0);
   auto run=begin(witness);assert(run);const auto released_before=released_failed_roots;
   PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);
   selected_parent=inserted;session_old_handle=old_root;session_parent_handle=parent_root;session_cache_handle=cache_root;
   transaction_root::Iterator iter{0,&holder};
   // Refund inside the real selected original Finish must retain its source
   // views until it returns. The sole Session then closes cleanup before Run
   // can leave its Holder/DEX lifetime, preserving source class status 14.
   static size_t* cleanup_count;cleanup_count=&released_failed_roots;
   static size_t count_before;count_before=released_before;
   assert(!native_hierarchy_session::Session::Current()->Finish(&actual_owner,&holder,&iter,+[](void*owner,transaction_root::Holder*h,void*i){
    assert(SessionFinish(owner,h,i));Put32(session_allocated,offsetof(class_metadata::Class17,status),14u<<28);
    native_schema_run_state::RunState::Current()->Refund();assert(*cleanup_count==count_before);
    assert(Read32(session_allocated,offsetof(class_metadata::Class17,status))>>28==14);return false;
   },&error));
   assert(released_failed_roots==released_before+1&&Read32(session_allocated,offsetof(class_metadata::Class17,status))>>28==14);
   class_remove_proxy(&linker,&pauser);assert(run->End(&actual_driver,false));run.reset();assert(SchemaTypedGlobals()==typed_globals_before_begin);
   Put32(array,12+11*4,0);Put32(array,12+12*4,0);
  }
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);
 std::cout<<"Actual RunState aggregate: failed Begin retry, repeated refund/End, early Commit/End abort guards, wrong-phase Finalize noalloc/noOS, actual Finish+GCmove+verification+Collected stopped refusal+rollback+unpublished initialized-14 discard+End; ART/JNI leaf fixtures only\n";
}

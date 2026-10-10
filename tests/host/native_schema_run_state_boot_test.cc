#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#include "pipeline_fixture.inc"
#include "cleanup_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_schema_run_state.h"
#include "native_schema_run_state_mapping_fixture.h"
#include "native_schema_run_state_typed_fixture.h"
#include "native_schema_run_state_public_fixture.h"
#include "native_schema_run_state_cache_roles_fixture.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_schema_run_state_boot_public_fixture.h"
#include "platform/common/native_boot_dex_identity.h"
#include "native_schema_run_state_boot_frames_fixture.h"
#pragma clang diagnostic pop
const char* boot_case="";
int main(int argc,char**argv){
 if(argc>1)boot_case=argv[1];
#include "root_setup.inc"
#include "pipeline_setup.inc"
#include "aggregate_owner_setup.inc"
  events.reserve(512);
  std::vector<unsigned char> before_bytes=File(RUN_STATE_BEFORE_DEX_FIXTURE);
  auto* before_file=static_cast<dex_mapping_abi::Prefix*>(original_file);
  *before_file={nullptr,before_bytes.data(),before_bytes.size(),before_bytes.data(),before_bytes.size()};
  static_cast<dex_mapping_abi::Prefix*>(incoming_file)->size=d.mapping.size;
  const uint32_t field_indices[]{1,3,0,2};for(size_t i=0;i<4;++i){old_fields[i]->dex_field_index=field_indices[i];new_fields[i]->dex_field_index=field_indices[i];}
  evolution::MappedDexMetadata before_mapping{before_bytes.data(),before_bytes.size(),before_bytes.data(),before_bytes.size(),0};
  assert(evolution::ReadMappedDexDeclarations(before_mapping,&proposal.before,&error));
  assert(proposal.before.descriptor==proposal.after.descriptor&&proposal.before.superclass==proposal.after.superclass);
  std::vector<unsigned char> graph_bytes=File(RUN_STATE_GRAPH_DEX_FIXTURE);
  ConfigureDefiningGraph(graph_bytes,*d.metadata);
  if(!cache_role_fixture.Prepare(before_bytes,&env,&ti,*d.metadata,*d.roots,&error)){std::cerr<<"matched Public cache roles: "<<error<<"\n";std::abort();}
  cache_role_fixture.Witness(&witness);typed_globals_before_begin=SchemaTypedGlobals();
  guard_expected_loader=app_loader;
  std::vector<unsigned char> boot_bytes=File(RUN_STATE_BOOT_DEX_FIXTURE);
  native_boot_method_catalog::Catalog boot_catalog;native_abstract_instances::Service vtables;
  if(!CalibrateBootPublic(owner_hooks,&env,&ti,*d.metadata,method_model,boot_bytes,&boot_catalog,&vtables,&error)){std::cerr<<"Boot public: "<<error<<"\n";std::abort();}
  boot_compiled_method=BootCreatedMethod();boot_enumeration=BootForEach;
  Put32(BootCreatedClass(),offsetof(class_metadata::Class17,iftable),Read32(parent_class,offsetof(class_metadata::Class17,iftable)));
  auto* empty_vtable=heap.New(16);Put32(empty_vtable,0,uint32_t(uintptr_t(BootLongArrayClass())));Put32(empty_vtable,8,0);
  for(void* klass:{static_cast<void*>(old_class),static_cast<void*>(new_class),static_cast<void*>(parent_class)}){Put32(klass,offsetof(class_metadata::Class17,vtable),uint32_t(uintptr_t(empty_vtable)));Put32(klass,offsetof(class_metadata::Class17,object_size),64);Put32(klass,offsetof(class_metadata::Class17,class_size),160);Put32(klass,sizeof(class_metadata::Class17),0);}
  receiver_guard::SourceProducer boot_source;assert(boot_source.Bind(owner_hooks,stack_visitor_service::Declaration::kAndroid13To16,code_item_metadata::kTagged,*d.metadata,&linker,Current,&error));
  deps.mapping.refresh_stopped=BootRefreshMaps;
  deps.boot_catalog=&boot_catalog;deps.boot_vtables=&vtables;deps.boot_code=&boot_source;
  native_schema_policy::Plan policy;assert(native_schema_policy::Plan::Prepare(proposal.before,proposal.after,&policy,&error));
  std::vector<native_schema_policy::Plan> policies{policy};
  deps.members.retirement.owner=&retired_records;deps.members.schemas=&policies;deps.members.expected_removal=native_member_provider::Removal::kSchemaEvolution;
  native_schema_guard_verifier::Verifier verifier;guard_expected_loader=app_loader;guard_base_class=base;auto* base_methods=static_cast<unsigned char*>(heap.New(40));Put32(base_methods,0,1);base->methods=uintptr_t(base_methods);auto* base_method=new(base_methods+8)abi::arm64::android12::Method12{};base_method->declaring_class=uint32_t(uintptr_t(base));base_method->access_flags=1;base_method->dex_method_index=1;guard_base_method=base_method;ConfigurePublicGuard(table,funcs);ConfigureSchemaTypedPublic(table,funcs);funcs.GetClassLoader=BootClassLoader;funcs.GetClassMethods=BootClassMethods;funcs.GetMethodName=BootMethodName;funcs.GetMethodModifiers=BootMethodFlags;
  native_schema_guard_verifier::Bindings guards;guards.vm=static_cast<JavaVM*>(aggregate_context.vm);guards.metadata=d.metadata;guards.methods=&method_model;
  guards.roots={nullptr,GuardScope,GuardOracle,GuardDecode,GuardElement};guards.decode_global=GuardDecode;guards.decode_method=BootGuardMethod;guards.decode_field=GuardField;guards.decode_native_field=GuardNativeField;guards.stage_field=GuardStageField;guards.method_class=GuardMethodClass;guards.same_method=BootGuardSame;guards.readable=GuardReadable;guards.writable=GuardReadable;guards.extent=GuardExtent;
  if(!verifier.Stage(&env,&ti,{reinterpret_cast<jclass>(old_class)},guards,&error)){std::cerr<<"actual Stage: "<<error<<"\n";std::abort();}
  assert(verifier.MatchesAffected(&env,{reinterpret_cast<jclass>(old_class)},&error));
  deps.members.schema_guards=&verifier;deps.guards=&verifier;
  auto* same_parent_root=pool.Add(base,&error);assert(same_parent_root);
  // The shared generator includes the baseline's initially rejecting public
  // storage oracle. Exercise its actual refusal and cleanup here too before
  // allowing that same oracle to publish its real original cache addresses.
  error.clear();
  assert(!native_schema_run_state::RunState::Begin(deps,&actual_driver,aggregate_context,&ti,{proposal},witness,&error));
  assert(public_state.resolved&&public_state.reads==1&&!native_schema_run_state::RunState::Current());
  assert(error=="Source cache public storage callback rejected original storage witness");
  public_state.accept=true;error.clear();
  auto run=native_schema_run_state::RunState::Begin(deps,&actual_driver,aggregate_context,&ti,{proposal},witness,&error);
  if(!run){std::cerr<<"matched Begin: "<<error<<"\n";std::abort();}
  PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);
  selected_parent=base;session_old_handle=old_root;session_parent_handle=same_parent_root;session_cache_handle=cache_root;
  transaction_root::Iterator iter{0,&holder};
  if(!session_finish_proxy(&actual_owner,&holder,&iter)){std::cerr<<"matched Finish: "<<actual_failure_message<<"\n";std::abort();}
  assert(session_allocated&&session_allocated==relocated_new&&allocated==2);
  assert(run->BeforeVerification(&actual_owner,&iter,&error));
  assert(verifier.CheckWindow(&env,&ti,{reinterpret_cast<jclass>(old_class)},native_schema_guard_verifier::Window::Verified,&error));
  run->AfterVerification(&actual_owner,&iter,true);
  source_commit::Plan plan;plan.mirror=old_class;plan.old_classes={old_class};plan.new_classes={session_allocated};
  auto* collected=heap.New(16);Put32(collected,0,uint32_t(uintptr_t(array_type)));Put32(collected,8,1);Put32(collected,12,uint32_t(uintptr_t(session_allocated)));plan.new_classes_root=collected;plan.class_root_origins={{collected,0}};Put32(array,12+11*4,uint32_t(uintptr_t(collected)));
  assert(verifier.CheckWindow(&env,&ti,{reinterpret_cast<jclass>(old_class)},native_schema_guard_verifier::Window::Collected,&error));
  if(!verifier.Seal(plan,&error)){std::cerr<<"actual guard Seal: "<<error<<"\n";std::abort();}
  std::vector<native_schema_policy::GuardRequest> requests;for(const auto& required:policy.Requirements())requests.push_back({0,app_loader,required});
  if(!verifier.PrepareGuardProofs(plan,requests,&error)){std::cerr<<"actual guard Prepare: "<<error<<"\n";std::abort();}
  boot_bad_pc=!std::strcmp(boot_case,"pc");boot_bad_value=!std::strcmp(boot_case,"value");boot_bad_mask=!std::strcmp(boot_case,"mask");boot_bad_cursor=!std::strcmp(boot_case,"cursor");
  if(!std::strcmp(boot_case,"body")){uint32_t code_offset=0;assert(BootCodeOffset(boot_bytes,0,&code_offset));boot_bytes[code_offset+17]=0x10;}
  if(!std::strcmp(boot_case,"prefix"))Put32(session_allocated,offsetof(class_metadata::Class17,object_size),4);
  if(!std::strcmp(boot_case,"declaring"))Put32(boot_compiled_method,0,uint32_t(uintptr_t(old_class)));
  native_owned_removal::RootedGraph graph;graph.holder=&holder;assert(transaction_root::ReadMetadata(holder,1,transaction_root::OwnerFamily::kAndroid15,&graph.metadata));
  if(!run->Collected(graph,plan,&error)){std::cerr<<"matched Collected: "<<error<<"\n";assert(*boot_case);if(std::strcmp(boot_case,"declaring"))assert(run->Frames().frames==1);else assert(!run->Frames().frames);run->Refund();class_remove_proxy(&linker,&pauser);assert(run->End(&actual_driver,false));run.reset();roots.Clear();verifier.Clear(&env);boot_catalog.Clear(&env);assert(SchemaTypedGlobals()==typed_globals_before_begin);std::cout<<"Actual concrete Boot AOT prepublication refusal/rollback case="<<boot_case<<" error="<<error<<"\n";return true;}
  assert(!*boot_case);
  matched_plan=&plan;
  publication_proxy(&linker,{uintptr_t(cache)},{uintptr_t(app_loader)});
  owner_disable_proxy(&owner_heap,thread.data());OwnerNative(thread.data());unsigned char carrier;owner_stop_proxy(&carrier,s::kFinalCause,true);
  const source_commit::Plan* final_plan=nullptr;forbid_provider_allocation=true;
  auto facade=run->Finalize(&holder,&final_plan);assert(facade&&final_plan==&plan);assert(mappings.final_refreshes==1);const auto final_os_queries=method_identity::MappingOsQueries();
  assert(facade->Validate(plan));
  assert(run->Frames().unaffected_aot_frames==1&&run->Frames().aot_frames==1&&run->Frames().native_value_reads==1);
  assert(source_commit::Run(plan,*facade,{run.get(),[](const source_commit::Plan& value,void* owner)noexcept{static_cast<native_schema_run_state::RunState*>(owner)->Commit(value);}}));
  assert(method_identity::MappingOsQueries()==final_os_queries);forbid_provider_allocation=false;
  facade.reset();class_remove_proxy(&linker,&pauser);owner_resume_proxy(&carrier);OwnerRunnable(thread.data());owner_enable_proxy(&owner_heap,thread.data());
  assert(run->End(&actual_driver,true));run.reset();roots.Clear();verifier.Clear(&env);boot_catalog.Clear(&env);matched_plan=nullptr;assert(SchemaTypedGlobals()==typed_globals_before_begin);assert(schema_typed_walks>=4);
  assert(!gc&&!disabled&&!stopped&&replaced&&boot_quick.value==0&&Read32(boot_compiled_method,4)==9&&!BootFlag(boot_header));
  std::cout<<"Actual concrete Boot AOT owner, original async=false/unmarked header, full maps/value, unchanged Boot prefix, public/native source bytes and full RunState Stage+Seal+guard proofs, moving allocation, Collected, original publication, final native Validate+Commit+cleanup+End passed with no C++ allocation, explicit fresh final OS bounds capture, then zero OS mapping queries in Validate/Commit (publication has Android17 no-inner-GC semantics)\n";
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);
}

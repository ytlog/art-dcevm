#include "native_call_graph_execution_fixture.h"
#include "platform/common/native_call_graph_replacement.h"
int main(int argc,char**argv){
 assert(argc==4);const std::string scenario=argv[3];const bool abstract=scenario=="abstract";const bool native_all=scenario=="native-only";const bool native_target=scenario=="outgoing-native";const size_t selected=native_all?1:abstract||native_target?3:4;std::string error;
 drain_fixture::Environment environment;environment.Prepare(error);execution_fixture::Hooks hooks;
 jvmtiInterface_1 functions{};jvmtiEnv ti{&functions};native_schema_guard_verifier::Bindings bindings{};
 execution_fixture::Configure(environment,functions,bindings,argv[1]);
 if(scenario=="outgoing-loader"){old_class->class_loader=uint32_t(uintptr_t(parent_class));execution_fixture::replacement_loader=reinterpret_cast<jobject>(parent_class);}
 receiver_guard::SourceProducer source;assert(source.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,code_item_metadata::kStandard,environment.metadata,&linker,Current,&error,&::runtime));
 assert(redefinition_gate::InstallStartup(hooks,&::runtime,&ti,&error));redefinition_gate::Lease lease(&::runtime);assert(lease.Acquired());
 native_schema_guard_verifier::Verifier verifier;std::vector<jclass>affected{reinterpret_cast<jclass>(old_class)};
 assert(execution_fixture::Stage(verifier,&ti,affected,bindings,&error));
 native_call_graph_execution::Execution operation(environment.source,environment.threads);
 struct Before {native_call_graph_execution::Execution&operation;execution_fixture::Hooks&hooks;jvmtiEnv&ti;std::vector<jclass>&affected;native_schema_guard_verifier::Verifier&verifier;receiver_guard::SourceProducer&source;drain_fixture::Environment&environment;std::string&error;}before{operation,hooks,ti,affected,verifier,source,environment,error};
 assert(execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),[](const runnable_jni::Context&ctx,void*p){auto&r=*static_cast<Before*>(p);return r.operation.Prepare(r.hooks,&env,&r.ti,r.affected,ctx,r.verifier,r.source,r.environment.model,r.environment.metadata,stack_visitor_service::Declaration::kAndroid13To16,&r.error);},&before)==runnable_jni::Result::kSuccess);
 ExplicitMappings before_maps;before_maps.Cover(heap.base);before_maps.Cover(execution_fixture::dex.data());modern_field_metadata::ReadContext before_read{&before_maps,ExplicitMappings::Extent};execution_fixture::stopped_read=&before_read;
 drain_fixture::frames[0]={{execution_fixture::MethodAt(1),true}};drain_fixture::frames[1]={{execution_fixture::MethodAt(2),true}};drain_fixture::frames[2]={{nullptr,false}};
 assert(operation.InspectActivation(thread.size(),before_read).status==native_call_graph_execution::Status::InspectedDrained);
 // The independent leaf selection remains a comparison check below. Its owner
 // must also be calibrated before entering the original loading pause.
 native_selective_interpretation::Service selection;assert(selection.CalibrateOwner(hooks,&env,&error));
 auto incoming_bytes=File(argv[2]);native_call_graph_incoming::Inventory incoming;
 assert(incoming.Capture(incoming_bytes,0,"LFixture;",&error));
 LinkDex(new_class,incoming_bytes);new_class->access_flags=abstract?0x401:1;new_class->dex_class_def_index=0;new_class->dex_type_index=1;
 if(scenario=="outgoing-loader"){new_class->class_loader=uint32_t(uintptr_t(parent_class));execution_fixture::replacement_loader=reinterpret_cast<jobject>(parent_class);}
 // Independent native Method12 array model, intentionally interleaved rather
 // than following incoming class_data's direct/virtual record order.
 auto*array=static_cast<unsigned char*>(heap.New(8+4*32));Put32(array,0,4);new_class->methods=uintptr_t(array);
 std::array<void*,4>native{};const uint32_t order[]{2,0,3,1};
 size_t p=Read32(incoming_bytes.data(),Read32(incoming_bytes.data(),100)+24);
 auto leb=[&](){uint32_t n=0;for(unsigned s=0;s<35;s+=7){auto b=incoming_bytes[p++];n|=uint32_t(b&127)<<s;if(!(b&128))return n;}std::abort();};
 uint32_t counts[4];for(auto&n:counts)n=leb();for(size_t i=0;i<counts[0]+counts[1];++i){leb();leb();}
 std::array<uint32_t,4>offsets{},modifiers{};for(unsigned kind=2;kind<4;++kind){uint32_t index=0;for(size_t i=0;i<counts[kind];++i){index+=leb();modifiers[index]=leb();offsets[index]=leb();}}
 for(size_t n=0;n<4;++n){auto*m=reinterpret_cast<abi::arm64::android12::Method12*>(array+8+32*n);const auto index=order[n];
  native[index]=m;m->declaring_class=uint32_t(uintptr_t(new_class));m->dex_method_index=index;m->access_flags=modifiers[index]|((modifiers[index]&0x400u)?0x100u:0);m->data=offsets[index]?uintptr_t(incoming_bytes.data()+offsets[index]):uintptr_t(&binding_value);}
 auto old_array=reinterpret_cast<jobjectArray>(Array({old_class}));auto new_array=reinterpret_cast<jobjectArray>(Array({new_class}));
 std::vector<void*>holder_slots(13);holder_slots[11]=old_array;holder_slots[12]=new_array;
 void*root_array=Array(holder_slots);uint32_t array_handle=uint32_t(uintptr_t(root_array));
 runtime_services::Snapshot runtime;runtime.runtime=early_owner_fixture::runtime;native_source_class_loading::Service loading_service;
 assert(native_source_class_loading::Service::Bind(hooks,runtime,&loading_service,&error));assert(native_source_class_loading::Install(hooks,loading_service,&error));
 abi::arm64::android_common::RedefinerPrefix driver{&ti,JVMTI_ERROR_INTERNAL,0,runtime.runtime,thread.data(),0};
 runnable_jni::Context context{thread.data(),&env,&::runtime};native_source_class_loading::Run loading(loading_service,&driver,context);assert(loading.OwnsDriver(&driver));
 struct Pauser{const void*vtable;void*self;uint8_t running;}pauser{execution_fixture::pauser_vtable+2,thread.data(),1};int callbacks;
 execution_fixture::add_proxy(&callbacks,&pauser);
 abi::arm64::android_common::ClassRedefinitionPrefix owner{};owner.driver=&driver;
 auto*begin=reinterpret_cast<const unsigned char*>(&owner);transaction_root::Owners owners{begin,begin+sizeof(owner),begin+sizeof(owner)};
 uint64_t bits=1;transaction_root::Bits flags{&bits,1,1};transaction_root::Holder holder{&array_handle,&owners,flags,flags,flags};transaction_root::Iterator iterator{0,&holder};
 assert(!loading.OwnsFinishedHolder(&holder,1));
 assert(loading.BeforeFinish(&owner,&holder,&iterator));assert(!loading.OwnsFinishedHolder(&holder,1));
 loading.AfterFinish(&owner,true);assert(loading.OwnsFinishedHolder(&holder,1));
 assert(!loading.OwnsFinishedHolder(&owner,1)&&!loading.OwnsFinishedHolder(&holder,2));
 native_owned_removal::RootedGraph graph;graph.holder=&holder;graph.metadata.count=1;
 graph.partitions.push_back({old_array,new_array,nullptr,nullptr,0,1,0});
 native_call_graph_replacement::Binding replacement;
 struct Request {native_call_graph_replacement::Binding&replacement;execution_fixture::Hooks&hooks;jvmtiEnv&ti;native_source_class_loading::Run&loading;void*driver;native_owned_removal::RootedGraph&graph;native_call_graph_incoming::Inventory&incoming;drain_fixture::Environment&environment;receiver_guard::SourceProducer&source;std::string&error;}request{replacement,hooks,ti,loading,&driver,graph,incoming,environment,source,error};
 const auto callback=[](const runnable_jni::Context&ctx,void*p){auto&r=*static_cast<Request*>(p);return r.replacement.Stage(r.hooks,ctx,&r.ti,r.loading,r.driver,r.graph,0,0,r.incoming,r.environment.store,r.environment.metadata,r.environment.model,r.source,&r.error);};
 const auto refused=[&](native_owned_removal::RootedGraph candidate){
  native_call_graph_replacement::Binding rejected;
  Request sample{rejected,hooks,ti,loading,&driver,candidate,incoming,environment,source,error};
  assert(execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),callback,&sample)!=runnable_jni::Result::kSuccess);
  assert(!error.empty()&&rejected.Selection().empty()&&!rejected.DeclaredCount());
 };
 auto wrong=graph;wrong.metadata.count=2;refused(wrong);
 wrong=graph;wrong.holder=&owner;refused(wrong);
 wrong=graph;wrong.partitions[0].old_classes=new_array;wrong.partitions[0].new_classes=old_array;refused(wrong);
 Put32(array,0,5);refused(graph);Put32(array,0,4);
 const auto public_loader=new_class->class_loader;new_class->class_loader=public_loader?0:uint32_t(uintptr_t(parent_class));refused(graph);new_class->class_loader=public_loader;
 auto result=execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),callback,&request);
 if(result!=runnable_jni::Result::kSuccess){std::cerr<<"Actual replacement Stage: "<<error<<"\n";return 2;}
 assert(replacement.DeclaredCount()==4&&replacement.Selection().size()==selected-1);
 const std::vector<native_call_graph_replacement::Definition>definitions{{0,0,&incoming}};
 struct BatchRequest {native_call_graph_replacement::Batch&batch;Request&request;const std::vector<native_call_graph_replacement::Definition>&definitions;} ;
 const auto batch_callback=[](const runnable_jni::Context&ctx,void*p){auto&b=*static_cast<BatchRequest*>(p);auto&r=b.request;return b.batch.Stage(r.hooks,ctx,&r.ti,r.loading,r.driver,r.graph,b.definitions,r.environment.store,r.environment.metadata,r.environment.model,r.source,&r.error);};
 for(const auto&bad:std::vector<std::vector<native_call_graph_replacement::Definition>>{{},{{0,1,&incoming}},{{0,0,nullptr}},{{0,0,&incoming},{0,0,&incoming}}}){
  native_call_graph_replacement::Batch rejected;BatchRequest sample{rejected,request,bad};
  assert(execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),batch_callback,&sample)!=runnable_jni::Result::kSuccess);
  assert(rejected.Selection().empty()&&!rejected.ClassCount());
 }
 // Exercise complete-slot coverage and failure after a first binding actually
 // succeeded. Even a supplied graph with duplicate Class roots cannot publish.
 auto repeated=graph;repeated.partitions[0].class_count=2;
 auto repeated_old=reinterpret_cast<jobjectArray>(Array({old_class,old_class}));
 auto repeated_new=reinterpret_cast<jobjectArray>(Array({new_class,new_class}));
 repeated.partitions[0].old_classes=repeated_old;repeated.partitions[0].new_classes=repeated_new;
 Put32(root_array,12+11*4,uint32_t(uintptr_t(repeated_old)));Put32(root_array,12+12*4,uint32_t(uintptr_t(repeated_new)));
 native_call_graph_incoming::Inventory incompatible;assert(incompatible.Capture(execution_fixture::dex,0,"LFixture;",&error));
 for(const auto&candidate:std::vector<std::vector<native_call_graph_replacement::Definition>>{
   {{0,0,&incoming},{0,0,&incoming}},{{0,0,&incoming},{0,1,&incoming}},{{0,0,&incoming},{0,1,&incompatible}}}){
  native_call_graph_replacement::Batch rejected;
  Request sample_request{replacement,hooks,ti,loading,&driver,repeated,incoming,environment,source,error};BatchRequest sample{rejected,sample_request,candidate};
  assert(execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),batch_callback,&sample)!=runnable_jni::Result::kSuccess);
  assert(!rejected.ClassCount()&&rejected.Selection().empty());
 }
 Put32(root_array,12+11*4,uint32_t(uintptr_t(old_array)));Put32(root_array,12+12*4,uint32_t(uintptr_t(new_array)));
 struct ReplacementRequest {native_call_graph_execution::Execution&operation;Request&request;const std::vector<native_call_graph_replacement::Definition>&definitions;}coordinator{operation,request,definitions};
 assert(execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),[](const runnable_jni::Context&ctx,void*p){auto&b=*static_cast<ReplacementRequest*>(p);auto&r=b.request;return b.operation.PrepareReplacements(r.hooks,ctx,&r.ti,r.loading,r.driver,r.graph,b.definitions,r.environment.store,&r.error);},&coordinator)==runnable_jni::Result::kSuccess);
 assert(operation.Replacements().ClassCount()==1&&operation.Replacements().Selection().size()==selected-1);
 const auto&outgoing=operation.Replacements();const auto&nodes=outgoing.Nodes();const auto&edges=outgoing.Edges();
 assert(nodes.size()==4&&outgoing.OutgoingPlan().failure==native_call_graph::Failure::None);
 const auto ordinal=[&](void*m){for(size_t i=0;i<nodes.size();++i)if(nodes[i].identity.native_method==m)return i;std::abort();};
 for(const auto&node:nodes)assert(node.identity.dex_namespace==incoming_bytes.data()&&node.identity.dex_namespace!=execution_fixture::dex.data()&&node.identity.loader_token==uint64_t(scenario=="outgoing-loader"));
 if(abstract)assert(nodes[ordinal(native[1])].kind==native_call_graph::MethodKind::Managed);
 const size_t calls=native_all?0:abstract?1:2;
 assert(edges.size()==calls&&operation.LastReport().replacement_calls==calls);
 const bool gap=scenario=="outgoing-wrong-direct"||scenario=="outgoing-virtual"||scenario=="outgoing-external";
 assert(outgoing.OutgoingPlan().complete==!gap&&outgoing.OutgoingPlan().gaps.size()==size_t(gap));
 assert(operation.LastReport().replacement_call_gaps==size_t(gap));
 for(const auto&edge:edges){
  assert(edge.dex_pc==0);
  if(edge.caller==ordinal(native[1])){
   assert(edge.target==(gap?SIZE_MAX:ordinal(native[2])));
   assert(edge.coverage==(gap?native_call_graph::Coverage::Unresolved:native_call_graph::Coverage::Exact));
   assert(edge.kind==(scenario=="outgoing-direct"||scenario=="outgoing-wrong-direct"?native_call_graph::InvokeKind::Direct:
    scenario=="outgoing-virtual"||scenario=="outgoing-external"?native_call_graph::InvokeKind::Virtual:native_call_graph::InvokeKind::Static));
  }else assert(edge.caller==ordinal(native[2])&&edge.target==ordinal(native[3])&&edge.coverage==native_call_graph::Coverage::Exact);
 }
 if(native_target){assert(nodes[ordinal(native[3])].kind==native_call_graph::MethodKind::Native);
  assert(outgoing.OutgoingPlan().native_boundaries==std::vector<size_t>{ordinal(native[3])}&&operation.LastReport().replacement_native_boundaries==1);}
 if(abstract)for(const auto&m:replacement.Selection())assert(m.method!=native[1]);
 for(const auto&m:replacement.Selection())assert(m.method==native[m.dex_index]&&m.method!=execution_fixture::MethodAt(m.dex_index));
 std::vector<native_selective_interpretation::Method>combined{{execution_fixture::MethodAt(1),environment.root,environment.root,1,1}};
 combined.insert(combined.end(),replacement.Selection().begin(),replacement.Selection().end());
 assert(selection.Prepare(hooks,environment.model,environment.metadata,environment.store,combined.data(),combined.size(),&error));
 native_call_graph_stack::Service stack;native_call_graph_stack::Service::Prepared stack_prepared;
 assert(stack.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,&error)&&stack.Prepare(selection,&stack_prepared,&error));
 drain_fixture::frames[0]={{execution_fixture::MethodAt(1),true},{native[2],!native_all}};
 drain_fixture::frames[1]={{native[3],!native_all}};drain_fixture::frames[2]={{nullptr,false}};
 auto relocated_bytes=incoming_bytes;
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(execution_fixture::dex.data());maps.Cover(incoming_bytes.data());maps.Cover(relocated_bytes.data());maps.Cover(&holder);maps.Cover(&array_handle);
 modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};execution_fixture::stopped_read=&read;
 using Status=native_call_graph_execution::Status;
 const auto refund=[&](bool live_frames,bool expect_invalid=false){
  struct Request {native_call_graph_execution::Execution&operation;native_source_class_loading::Run&loading;void*driver;modern_field_metadata::ReadContext&read;bool live,invalid;const std::string&scenario;decltype(native)&methods;}r{operation,loading,&driver,read,live_frames,expect_invalid,scenario,native};
  const auto inspect=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<Request*>(p);
   const auto os=method_identity::MappingOsQueries();const auto jni=jni_calls;const auto removed=drain_fixture::undeopt_calls;forbid_provider_allocation=true;
   if(r.invalid){auto report=r.operation.RefundReplacements(stop,r.loading,r.driver,r.read);assert(!report.owned_entries_removed&&drain_fixture::undeopt_calls==removed);}
   else {
    assert(!r.operation.RefundReplacements(stop,r.loading,nullptr,r.read).owned_entries_removed);
    if(r.live){auto refused=r.operation.RefundReplacements(stop,r.loading,r.driver,r.read);
     assert(!refused.owned_entries_removed&&refused.inspected.walk_complete&&drain_fixture::undeopt_calls==removed);
     assert(std::string_view(refused.failure)=="call-graph-execution-refund-replacement-frame-still-retains-method");}
    // Existing vectors are retained; changing model frame slots allocates no
    // storage. Incoming frames disappear, while installed BEFORE frames stay.
    for(auto&frames:drain_fixture::frames)for(auto&frame:frames)
     if(frame.method==r.methods[1]||frame.method==r.methods[2]||frame.method==r.methods[3]){frame.method=nullptr;frame.shadow=false;}
    if(r.scenario=="refund-failure"){
     drain_fixture::fail_undeopt=drain_fixture::undeopt_calls+1;
     auto failed=r.operation.RefundReplacements(stop,r.loading,r.driver,r.read);
     assert(!failed.owned_entries_removed&&failed.entries.removed==1&&failed.entries.pending==2);
     drain_fixture::fail_undeopt=SIZE_MAX;
    }
    auto report=r.operation.RefundReplacements(stop,r.loading,r.driver,r.read);
    assert(report.owned_entries_removed&&report.entries.complete&&!report.entries.pending);
    assert(r.operation.LastReport().status==Status::FinalRefused&&!r.operation.LastReport().replacement_bodies_drained);
    if(r.scenario=="partial-activation")assert(report.entries.removed==1&&report.entries.already_absent==1);
    for(size_t index:{size_t(1),size_t(2),size_t(3)})
     assert(drain_fixture::Query(early_owner_fixture::instrumentation,r.methods[index])==(r.scenario=="preexisting"&&index==2));
    if(r.scenario=="preexisting")assert(report.entries.foreign_retained==1);
    // Repeating cleanup never invokes Undeoptimize on a removed owned entry.
    const auto count=drain_fixture::undeopt_calls;auto repeated=r.operation.RefundReplacements(stop,r.loading,r.driver,r.read);
    assert(repeated.owned_entries_removed&&!repeated.entries.removed&&drain_fixture::undeopt_calls==count);
    assert(drain_fixture::Query(early_owner_fixture::instrumentation,execution_fixture::MethodAt(1))&&drain_fixture::Query(early_owner_fixture::instrumentation,execution_fixture::MethodAt(2)));
    if(r.scenario=="preexisting")drain_fixture::Undeopt(early_owner_fixture::instrumentation,r.methods[2]); // independent foreign owner
   }
   forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os&&jni_calls==jni);return true;
  };
  {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&r,inspect));}
  assert(!stopped&&!locked&&!drain_fixture::gc);
 };
 if(scenario=="preexisting"){
  auto add=[](void*p,native_source_stop::Session&)noexcept{drain_fixture::Deopt(early_owner_fixture::instrumentation,p);return true;};
  native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),native[2],add));
 }
 if(scenario=="partial-activation")drain_fixture::fail_deopt=drain_fixture::deopt_calls+1;
 if(scenario=="quick")drain_fixture::frames[0][1].shadow=false;
 if(scenario=="before-quick")drain_fixture::frames[0][0].shadow=false;
 if(scenario=="replacement-generation"){
  environment.store.Clear(&env);assert(environment.store.Stage(&env,reinterpret_cast<jobject>(old_class),&environment.root,&error));
 }
 if(scenario=="body-changed")reinterpret_cast<unsigned char*>(static_cast<abi::arm64::android12::Method12*>(native[2])->data)[16]^=1;
 const auto inspected=operation.InspectActivation(thread.size(),read);
 if(inspected.status==Status::InspectionRefused)std::cerr<<"Inspection refusal: "<<(inspected.failure?inspected.failure:"missing diagnostic")<<"\n";
 assert(!stopped&&!locked&&!drain_fixture::gc&&!operation.InspectionStopped());
 if(scenario=="quick"||scenario=="before-quick"||scenario=="body-changed"||scenario=="replacement-generation"||scenario=="partial-activation"){
  assert(inspected.status==((scenario=="body-changed"||scenario=="replacement-generation"||scenario=="partial-activation")?Status::InspectionRefused:Status::UnsupportedActiveCompiled));
  assert(!inspected.compiled_drained&&!inspected.replacement_bodies_drained);
  if(scenario=="body-changed")reinterpret_cast<unsigned char*>(static_cast<abi::arm64::android12::Method12*>(native[2])->data)[16]^=1;
  refund(true,scenario=="replacement-generation");
  execution_fixture::remove_proxy(&callbacks,&pauser);assert(loading.Complete(true));verifier.Clear(&env);environment.store.Clear(&env);
  std::cout<<"Execution actual replacement/before quick or incoming body change refused and world resumed PASS\n";return 0;
 }
 assert(inspected.status==Status::ReplacementInspectedDrained&&inspected.replacement_bodies_drained&&!inspected.compiled_drained);
 assert(inspected.scope==native_call_graph_execution::GraphScope::InstalledBeforeAndReplacementBodies);
 assert(inspected.replacement_activation.activated==selected-1-(scenario=="preexisting"?1:0)&&inspected.replacement_activation.selected==selected-1);
 assert(inspected.before_reinspected.walk_complete&&inspected.before_reinspected.selected_shadow==1&&inspected.before_reinspected.registered_threads==3);
 if(native_all)assert(!inspected.replacement_inspected.walk_complete&&operation.Replacements().Selection().empty()&&inspected.before_reinspected.boundary_owners_required);
 else assert(inspected.replacement_inspected.walk_complete&&inspected.replacement_inspected.selected_shadow==size_t(native_target?1:2)&&inspected.replacement_inspected.registered_threads==3);
 struct UnionFinal {native_call_graph_execution::Execution&operation;native_source_class_loading::Run&loading;void*driver;modern_field_metadata::ReadContext&read;native_call_graph_replacement::Binding&binding;std::vector<unsigned char>&bytes;std::vector<unsigned char>&relocated;}union_final{operation,loading,&driver,read,replacement,incoming_bytes,relocated_bytes};
 const auto union_refused=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<UnionFinal*>(p);const auto os=method_identity::MappingOsQueries();const auto jni=jni_calls;forbid_provider_allocation=true;
  assert(r.operation.Replacements().Validate(stop,r.read));
  // Same DEX content at another address preserves the independent binding,
  // but revokes the graph's captured namespace identity. No allocation/GC is
  // performed by this model mutation inside the actual stopped callback.
  auto*cache=reinterpret_cast<unsigned char*>(uintptr_t(new_class->dex_cache));dex_mapping_abi::Prefix*prefix=nullptr;
  std::memcpy(&prefix,cache+offsetof(abi::arm64::android11::LegacyCache,dex_file),8);
  prefix->begin=r.relocated.data();prefix->data_begin=r.relocated.data();
  for(const auto&selected:r.operation.Replacements().Selection()){auto*m=static_cast<abi::arm64::android12::Method12*>(selected.method);m->data=uintptr_t(r.relocated.data())+(m->data-uintptr_t(r.bytes.data()));}
  assert(r.binding.Validate(stop,r.read)&&!r.operation.Replacements().Validate(stop,r.read));
  prefix->begin=r.bytes.data();prefix->data_begin=r.bytes.data();
  for(const auto&selected:r.operation.Replacements().Selection()){auto*m=static_cast<abi::arm64::android12::Method12*>(selected.method);m->data=uintptr_t(r.bytes.data())+(m->data-uintptr_t(r.relocated.data()));}
  assert(r.operation.Replacements().Validate(stop,r.read));
  const auto report=r.operation.ValidateFinal(stop,r.loading,r.driver,r.read);
  assert(report.status==Status::UnsupportedIncompleteGraph&&!report.compiled_drained&&!report.replacement_bodies_drained&&std::string_view(report.failure)=="call-graph-execution-replacement-union-resolution-closure-unavailable");
  forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os&&jni_calls==jni);return true;};
 {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&union_final,union_refused));}
 if(native_all){refund(false);execution_fixture::remove_proxy(&callbacks,&pauser);assert(loading.Complete(true));verifier.Clear(&env);environment.store.Clear(&env);
  std::cout<<"Execution native-only replacement has no interpreter body selection; BEFORE rescan observes native boundaries; complete union still refused PASS\n";return 0;}

 struct Final {native_call_graph_replacement::Binding&binding;native_selective_interpretation::Service&selection;native_call_graph_stack::Service&stack;native_call_graph_stack::Service::Prepared&stack_prepared;const native_cache::Service&threads;modern_field_metadata::ReadContext&read;decltype(native)&methods;void*array;uint32_t*handle;std::vector<unsigned char>&bytes;size_t selected=0,validations=0;}final{replacement,selection,stack,stack_prepared,environment.threads,read,native,array,&array_handle,incoming_bytes,selected,0};
 const auto inspect=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<Final*>(p);
  native_selective_interpretation::Report activation;assert(r.binding.Validate(stop,r.read));
  assert(r.selection.Activate(stop,thread.data(),r.read,&activation)&&activation.activated==0&&activation.already_interpreted==r.selected);
  const auto os=method_identity::MappingOsQueries();const auto jni=jni_calls;forbid_provider_allocation=true;
  assert(r.binding.Validate(stop,r.read));++r.validations;
  native_selective_interpretation::Report retained;assert(r.selection.ValidateRetained(stop,thread.data(),r.read,&retained)&&retained.already_interpreted==r.selected);
  auto scanned=r.stack.Scan(stop,thread.data(),r.threads,r.stack_prepared,r.read);
  assert(scanned.walk_complete&&scanned.selected_drained&&scanned.selected_shadow==size_t((static_cast<abi::arm64::android12::Method12*>(r.methods[3])->access_flags&0x100u)?2:3)&&scanned.registered_threads==3);
  drain_fixture::frames[0][1].shadow=false;
  scanned=r.stack.Scan(stop,thread.data(),r.threads,r.stack_prepared,r.read);
  assert(scanned.walk_complete&&!scanned.selected_drained&&scanned.drain_required&&scanned.selected_quick==1);
  drain_fixture::frames[0][1].shadow=true;
  auto*method=static_cast<abi::arm64::android12::Method12*>(r.methods[2]);
  const auto original_flags=method->access_flags;method->access_flags^=0x10u;assert(!r.binding.Validate(stop,r.read));method->access_flags=original_flags;
  const auto owner=method->declaring_class;method->declaring_class=uint32_t(uintptr_t(old_class));assert(!r.binding.Validate(stop,r.read));method->declaring_class=owner;
  const auto index=method->dex_method_index;method->dex_method_index=3;assert(!r.binding.Validate(stop,r.read));method->dex_method_index=index;
  auto*body=reinterpret_cast<unsigned char*>(method->data);body[16]^=1;assert(!r.binding.Validate(stop,r.read));body[16]^=1;
  const auto pointer=method->data;method->data+=4;assert(!r.binding.Validate(stop,r.read));method->data=pointer;
  Put32(r.array,0,3);assert(!r.binding.Validate(stop,r.read));Put32(r.array,0,4);
  const auto class_array=new_class->methods;new_class->methods=old_class->methods;assert(!r.binding.Validate(stop,r.read));new_class->methods=class_array;
  const auto class_flags=new_class->access_flags;new_class->access_flags^=0x10u;assert(!r.binding.Validate(stop,r.read));new_class->access_flags=class_flags;
  const auto loader=new_class->class_loader;new_class->class_loader=loader?0:uint32_t(uintptr_t(parent_class));assert(!r.binding.Validate(stop,r.read));new_class->class_loader=loader;
  const auto handle=*r.handle;*r.handle=0;assert(!r.binding.Validate(stop,r.read));*r.handle=handle;
  assert(r.binding.Validate(stop,r.read));++r.validations;
  forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os&&jni_calls==jni);return true;
 };
 {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&final,inspect));}
 assert(final.validations==2&&!stopped&&!locked&&!drain_fixture::gc);
 refund(true);
 // Invalidate the same roots, then independently restore the original objects.
 // The stale binding cannot authorize even identical array/class contents.
 environment.store.Clear(&env);assert(environment.store.Stage(&env,reinterpret_cast<jobject>(old_class),&environment.root,&error));
 const auto revoked=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<Final*>(p);assert(!r.binding.Validate(stop,r.read));return true;};
 {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&final,revoked));}
 execution_fixture::remove_proxy(&callbacks,&pauser);assert(!loading.OwnsFinishedHolder(&holder,1));assert(loading.Complete(true));verifier.Clear(&env);environment.store.Clear(&env);
 std::cout<<"Actual Execution BEFORE + complete replacement batch/Holder mapping + new selective activation/complete ThreadList/owned entry refund and live-frame refusal and stopped noallocation/JNI/OS identity revocation PASS; no complete union graph or schema commit\n";
}

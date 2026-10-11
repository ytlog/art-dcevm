#include "native_call_graph_execution_fixture.h"
#include "platform/common/native_call_graph_replacement.h"
int main(int argc,char**argv){
 assert(argc==4);const bool abstract=std::string(argv[3])=="abstract";const size_t selected=abstract?3:4;std::string error;
 drain_fixture::Environment environment;environment.Prepare(error);execution_fixture::Hooks hooks;
 jvmtiInterface_1 functions{};jvmtiEnv ti{&functions};native_schema_guard_verifier::Bindings bindings{};
 execution_fixture::Configure(environment,functions,bindings,argv[1]);
 receiver_guard::SourceProducer source;assert(source.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,code_item_metadata::kStandard,environment.metadata,&linker,Current,&error,&::runtime));
 native_selective_interpretation::Service selection;assert(selection.CalibrateOwner(hooks,&env,&error));
 auto incoming_bytes=File(argv[2]);native_call_graph_incoming::Inventory incoming;
 assert(incoming.Capture(incoming_bytes,0,"LFixture;",&error));
 LinkDex(new_class,incoming_bytes);new_class->access_flags=abstract?0x401:1;new_class->dex_class_def_index=0;new_class->dex_type_index=1;
 // Independent native Method12 array model, intentionally interleaved rather
 // than following incoming class_data's direct/virtual record order.
 auto*array=static_cast<unsigned char*>(heap.New(8+4*32));Put32(array,0,4);new_class->methods=uintptr_t(array);
 std::array<void*,4>native{};const uint32_t order[]{2,0,3,1};
 size_t p=Read32(incoming_bytes.data(),Read32(incoming_bytes.data(),100)+24);
 auto leb=[&](){uint32_t n=0;for(unsigned s=0;s<35;s+=7){auto b=incoming_bytes[p++];n|=uint32_t(b&127)<<s;if(!(b&128))return n;}std::abort();};
 uint32_t counts[4];for(auto&n:counts)n=leb();for(size_t i=0;i<counts[0]+counts[1];++i){leb();leb();}
 std::array<uint32_t,4>offsets{};for(unsigned kind=2;kind<4;++kind){uint32_t index=0;for(size_t i=0;i<counts[kind];++i){index+=leb();leb();offsets[index]=leb();}}
 for(size_t n=0;n<4;++n){auto*m=reinterpret_cast<abi::arm64::android12::Method12*>(array+8+32*n);const auto index=order[n];
  native[index]=m;m->declaring_class=uint32_t(uintptr_t(new_class));m->dex_method_index=index;m->access_flags=index==0?0x101:index==1?(abstract?0x501:1):9;m->data=offsets[index]?uintptr_t(incoming_bytes.data()+offsets[index]):uintptr_t(&binding_value);}
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
 const auto public_loader=new_class->class_loader;new_class->class_loader=uint32_t(uintptr_t(parent_class));refused(graph);new_class->class_loader=public_loader;
 auto result=execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),callback,&request);
 if(result!=runnable_jni::Result::kSuccess){std::cerr<<"Actual replacement Stage: "<<error<<"\n";return 2;}
 assert(replacement.DeclaredCount()==4&&replacement.Selection().size()==selected-1);
 if(abstract)for(const auto&m:replacement.Selection())assert(m.method!=native[1]);
 for(const auto&m:replacement.Selection())assert(m.method==native[m.dex_index]&&m.method!=execution_fixture::MethodAt(m.dex_index));
 std::vector<native_selective_interpretation::Method>combined{{execution_fixture::MethodAt(1),environment.root,environment.root,1,1}};
 combined.insert(combined.end(),replacement.Selection().begin(),replacement.Selection().end());
 assert(selection.Prepare(hooks,environment.model,environment.metadata,environment.store,combined.data(),combined.size(),&error));
 native_call_graph_stack::Service stack;native_call_graph_stack::Service::Prepared stack_prepared;
 assert(stack.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,&error)&&stack.Prepare(selection,&stack_prepared,&error));
 drain_fixture::frames[0]={{execution_fixture::MethodAt(1),true},{native[2],true}};
 drain_fixture::frames[1]={{native[3],true}};drain_fixture::frames[2]={{nullptr,false}};
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(execution_fixture::dex.data());maps.Cover(incoming_bytes.data());maps.Cover(&holder);
 modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};execution_fixture::stopped_read=&read;
 struct Final {native_call_graph_replacement::Binding&binding;native_selective_interpretation::Service&selection;native_call_graph_stack::Service&stack;native_call_graph_stack::Service::Prepared&stack_prepared;const native_cache::Service&threads;modern_field_metadata::ReadContext&read;decltype(native)&methods;void*array;uint32_t*handle;std::vector<unsigned char>&bytes;size_t selected=0,validations=0;}final{replacement,selection,stack,stack_prepared,environment.threads,read,native,array,&array_handle,incoming_bytes,selected,0};
 const auto inspect=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<Final*>(p);
  native_selective_interpretation::Report activation;assert(r.binding.Validate(stop,r.read));
  assert(r.selection.Activate(stop,thread.data(),r.read,&activation)&&activation.activated==r.selected);
  const auto os=method_identity::MappingOsQueries();const auto jni=jni_calls;forbid_provider_allocation=true;
  assert(r.binding.Validate(stop,r.read));++r.validations;
  native_selective_interpretation::Report retained;assert(r.selection.ValidateRetained(stop,thread.data(),r.read,&retained)&&retained.already_interpreted==r.selected);
  auto scanned=r.stack.Scan(stop,thread.data(),r.threads,r.stack_prepared,r.read);
  assert(scanned.walk_complete&&scanned.selected_drained&&scanned.selected_shadow==3&&scanned.registered_threads==3);
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
  const auto loader=new_class->class_loader;new_class->class_loader=uint32_t(uintptr_t(parent_class));assert(!r.binding.Validate(stop,r.read));new_class->class_loader=loader;
  const auto handle=*r.handle;*r.handle=0;assert(!r.binding.Validate(stop,r.read));*r.handle=handle;
  assert(r.binding.Validate(stop,r.read));++r.validations;
  forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os&&jni_calls==jni);return true;
 };
 {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&final,inspect));}
 assert(final.validations==2&&!stopped&&!locked&&!drain_fixture::gc);
 // Invalidate the same roots, then independently restore the original objects.
 // The stale binding cannot authorize even identical array/class contents.
 environment.store.Clear(&env);assert(environment.store.Stage(&env,reinterpret_cast<jobject>(old_class),&environment.root,&error));
 const auto revoked=[](void*p,native_source_stop::Session&stop)noexcept{auto&r=*static_cast<Final*>(p);assert(!r.binding.Validate(stop,r.read));return true;};
 {native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&final,revoked));}
 execution_fixture::remove_proxy(&callbacks,&pauser);assert(!loading.OwnsFinishedHolder(&holder,1));assert(loading.Complete(true));environment.store.Clear(&env);
 std::cout<<"Actual finished Holder roots + independent incoming/new ArtMethod mapping + BEFORE/new selective activation/complete ThreadList/new quick refusal and stopped noallocation/JNI/OS identity revocation PASS; no complete union graph or schema commit\n";
}

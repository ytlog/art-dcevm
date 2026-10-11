#include "native_call_graph_execution_fixture.h"
int main(int argc,char**argv){
 assert(argc==3);const std::string scenario=argv[2];std::string error;
 drain_fixture::Environment environment;environment.Prepare(error);execution_fixture::Hooks hooks;
 jvmtiInterface_1 functions{};jvmtiEnv ti{&functions};native_schema_guard_verifier::Bindings bindings{};
 execution_fixture::Configure(environment,functions,bindings,argv[1]);
 assert(redefinition_gate::InstallStartup(hooks,&::runtime,&ti,&error));redefinition_gate::Lease lease(&::runtime);assert(lease.Acquired());
 native_schema_guard_verifier::Verifier verifier;std::vector<jclass>affected{reinterpret_cast<jclass>(old_class)};
 if(!execution_fixture::Stage(verifier,&ti,affected,bindings,&error)){std::cerr<<"Actual Stage: "<<error<<"\n";return 2;}
 receiver_guard::SourceProducer source;assert(source.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,environment.model,environment.metadata,&linker,Current,&error));
 native_call_graph_execution::Execution operation(environment.source,environment.threads);
 struct Preparation {native_call_graph_execution::Execution&operation;execution_fixture::Hooks&hooks;jvmtiEnv&ti;std::vector<jclass>&affected;native_schema_guard_verifier::Verifier&verifier;receiver_guard::SourceProducer&source;drain_fixture::Environment&environment;std::string&error;}request{operation,hooks,ti,affected,verifier,source,environment,error};
 const size_t staged_enumerations=execution_fixture::public_enumerations;
 auto preparation_result=execution_fixture::bridge.Invoke(&env,reinterpret_cast<jobject>(executable[1]),[](const runnable_jni::Context&context,void*p){auto&r=*static_cast<Preparation*>(p);return r.operation.Prepare(r.hooks,&env,&r.ti,r.affected,context,r.verifier,r.source,r.environment.model,r.environment.metadata,stack_visitor_service::Declaration::kAndroid13To16,&r.error);},&request);if(preparation_result!=runnable_jni::Result::kSuccess){std::cerr<<"Actual Prepare: "<<error<<"\n";return 2;}
 assert(execution_fixture::public_enumerations>staged_enumerations);
 auto*keep=execution_fixture::MethodAt(1);auto*scalar=execution_fixture::MethodAt(2);
 drain_fixture::frames[0]={{keep,scenario!="compiled"},{scalar,true}};drain_fixture::frames[1]={{scalar,true}};drain_fixture::frames[2]={{nullptr,false}};
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(execution_fixture::dex.data());modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};execution_fixture::stopped_read=&read;
 if(scenario=="flags")Put32(keep,4,Read32(keep,4)|0x100u);
 if(scenario=="body")execution_fixture::dex[reinterpret_cast<abi::arm64::android12::Method12*>(scalar)->data-uintptr_t(execution_fixture::dex.data())+16]^=1;
 if(scenario=="generation"){verifier.Clear(&env);assert(execution_fixture::Stage(verifier,&ti,affected,bindings,&error));}
 auto inspected=operation.InspectActivation(thread.size(),read);assert(!stopped&&!drain_fixture::gc&&!locked&&!operation.InspectionStopped());
 using S=native_call_graph_execution::Status;
 if(scenario=="compiled"){assert(inspected.status==S::UnsupportedActiveCompiled&&!inspected.compiled_drained);std::cout<<"Original selected quick refused after actual resume PASS\n";verifier.Clear(&env);environment.store.Clear(&env);return 0;}
 if(scenario=="generation"){assert(inspected.status==S::InspectionRefused&&!inspected.compiled_drained);std::cout<<"Actual verifier Clear/restage owner generation refused PASS\n";verifier.Clear(&env);environment.store.Clear(&env);return 0;}
 if(scenario=="body"||scenario=="flags"){assert(inspected.status==S::InspectionRefused&&!inspected.compiled_drained);std::cout<<"Actual independent public body / native metadata "<<scenario<<" refused PASS\n";verifier.Clear(&env);environment.store.Clear(&env);return 0;}
 if(inspected.status!=S::InspectedDrained){std::cerr<<"Actual Inspect failure: "<<(inspected.failure?inspected.failure:"none")<<"\n";return 3;}
 runtime_services::Snapshot runtime;runtime.runtime=early_owner_fixture::runtime;native_source_class_loading::Service loading_service;
 assert(native_source_class_loading::Service::Bind(hooks,runtime,&loading_service,&error));assert(native_source_class_loading::Install(hooks,loading_service,&error));
 abi::arm64::android_common::RedefinerPrefix driver{&ti,JVMTI_ERROR_INTERNAL,0,runtime.runtime,thread.data(),0};
 runnable_jni::Context context{thread.data(),&env,&::runtime};native_source_class_loading::Run loading(loading_service,&driver,context);assert(loading.OwnsDriver(&driver));
 struct Pauser{const void*vtable;void*self;uint8_t running;}pauser{execution_fixture::pauser_vtable+2,thread.data(),1};int callbacks;
 execution_fixture::add_proxy(&callbacks,&pauser);assert(!loading.Paused());
 abi::arm64::android_common::ClassRedefinitionPrefix owner{};owner.driver=&driver;
 auto*begin=reinterpret_cast<const unsigned char*>(&owner);transaction_root::Owners owners{begin,begin+sizeof(owner),begin+sizeof(owner)};uint32_t array_handle=0;transaction_root::Holder holder{&array_handle,&owners,{},{},{}};transaction_root::Iterator iterator{0,&holder};
 if(scenario!="unpaused"){assert(loading.BeforeFinish(&owner,&holder,&iterator));loading.AfterFinish(&owner,true);assert(loading.Paused());}
 struct Final{native_call_graph_execution::Execution&operation;native_source_class_loading::Run&loading;void*driver;modern_field_metadata::ReadContext&read;native_call_graph_execution::Report report{};}final{operation,loading,&driver,read,{}};
 const auto inspect=[](void*p,native_source_stop::Session&session)noexcept{auto&r=*static_cast<Final*>(p);const auto os=method_identity::MappingOsQueries();forbid_provider_allocation=true;r.report=r.operation.ValidateFinal(session,r.loading,r.driver,r.read);forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os);return true;};
 {native_source_stop::Session session(environment.source);assert(session.Inspect(thread.data(),thread.size(),&final,inspect));}
 assert(!stopped&&!drain_fixture::gc&&!locked);
 assert(final.report.status==S::FinalRefused&&!final.report.compiled_drained);
 assert(final.report.failure);
 const char*expected=scenario=="unpaused"?"call-graph-execution-final-source-owner-order-generation-refused":"call-graph-execution-final-source-census-inventory-refused";
 assert(!std::strcmp(final.report.failure,expected));
 execution_fixture::remove_proxy(&callbacks,&pauser);if(scenario=="unpaused")assert(!loading.Complete(false));else assert(loading.Complete(true));
 verifier.Clear(&env);environment.store.Clear(&env);
 std::cout<<"Actual independent Verifier census / immutable DEX / original resolution / closed selective owner / Source Session / complete ThreadList / original ClassLoading FinalRefused (frozen transaction prerequisite absent) "<<scenario<<" PASS (host ART/JNI leaves; no GC/publication or incoming graph authority)\n";
}

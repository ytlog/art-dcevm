#include "native_call_graph_stack_fixture.h"
int main(){
 using namespace drain_fixture;std::string error;Environment environment;environment.Prepare(error);
 auto&hooks=environment.hooks;auto&owner_hooks=environment.owner_hooks;auto&metadata=environment.metadata;
 auto&model=environment.model;auto&store=environment.store;auto root=environment.root;
 auto&source=environment.source;auto&threads=environment.threads;
 native_selective_interpretation::Service selection;assert(selection.CalibrateOwner(owner_hooks,&early_owner_fixture::jni,&error));
 native_selective_interpretation::Method selected[]{ {reinterpret_cast<char*>(old_method)+32,root,root,1,1},{reinterpret_cast<char*>(old_method)+64,root,root,2,9} };
 assert(selection.Prepare(owner_hooks,model,metadata,store,selected,2,&error));
 native_call_graph_stack::Service scanner;assert(scanner.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,&error));native_call_graph_stack::Service::Prepared prepared;assert(scanner.Prepare(selection,&prepared,&error));assert(!scanner.Bind(hooks,stack_visitor_service::Declaration::kAndroid11,&error));
 native_selective_interpretation::Service unavailable;native_call_graph_stack::Service::Prepared rejected;assert(!scanner.Prepare(unavailable,&rejected,&error));
 abi::arm64::android12::Method12 runtime_method{};runtime_method.dex_method_index=UINT32_MAX;
 frames[0]={{selected[0].method,false},{selected[1].method,false},{&runtime_method,false},{old_method,false},{nullptr,false}};frames[1]={{selected[1].method,true}};frames[2]={{nullptr,false}};
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(&runtime_method);modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};
 struct Work{native_call_graph_stack::Service&scanner;native_call_graph_stack::Service::Prepared&prepared;native_selective_interpretation::Service&selection;native_cache::Service&threads;modern_field_metadata::ReadContext&read;ExplicitMappings&maps;int scenario=0;native_call_graph_stack::Report report;}work{scanner,prepared,selection,threads,read,maps,0,{}};
 const auto inspect=[](void*p,native_source_stop::Session&stop)noexcept{auto&w=*static_cast<Work*>(p);native_selective_interpretation::Report activated;
  if(!w.scenario){w.report=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);assert(!w.report.walk_complete&&walk_calls==0);assert(w.selection.Activate(stop,thread.data(),w.read,&activated)&&activated.requires_stack_drain);}
  const auto os=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  w.report=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os);return true;};
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.registered_threads==3&&work.report.visited_threads==3&&work.report.selected_quick==2&&work.report.selected_shadow==1&&work.report.drain_required&&!work.report.selected_drained&&work.report.native_boundaries==1&&work.report.runtime_boundaries==1&&work.report.transitions==2&&work.report.boundary_owners_required);
 frames[0][0].shadow=frames[0][1].shadow=true;work.scenario=1;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.selected_drained&&!work.report.drain_required&&work.report.selected_shadow==3&&work.report.boundary_owners_required);
 // Native Class relocation changes both independently retained roots and the
 // methods' declaring references. No old managed Class pointer may be cached.
 std::memcpy(new_class,old_class,160);Put32(root_carrier,12,uint32_t(uintptr_t(new_class)));
 for(const auto&m:selected)Put32(m.method,0,uint32_t(uintptr_t(new_class)));
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.selected_drained);
 Put32(selected[0].method,0,uint32_t(uintptr_t(old_class)));
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(!work.report.walk_complete&&std::strstr(work.report.failure,"declaring-root"));
 Put32(selected[0].method,0,uint32_t(uintptr_t(new_class)));
 const uint32_t saved_flags=Read32(selected[0].method,4);Put32(selected[0].method,4,saved_flags|0x100u);
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(!work.report.walk_complete);Put32(selected[0].method,4,saved_flags);
 // An unselected frame still requires a current source-readable Method span.
 maps.denied=&runtime_method;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(!work.report.walk_complete&&!work.report.selected_drained&&std::strstr(work.report.failure,"method-source-span"));maps.denied=nullptr;
 maps.denied=selected[0].method;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}assert(!work.report.walk_complete&&!work.report.selected_drained);maps.denied=nullptr;
 deoptimized.clear();
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}assert(!work.report.walk_complete&&!work.report.selected_drained);
 store.Clear(&env);bounded_managed_roots::Slot replacement;
 assert(store.Stage(&env,reinterpret_cast<jobject>(new_class),&replacement,&error)&&replacement.chunk==root.chunk&&replacement.index==root.index);
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(!work.report.walk_complete&&!work.report.selected_drained);
 store.Clear(&env);assert(!stopped&&!gc&&!locked);
 std::cout<<"Actual selective CalibrateOwner/JVMTI/attach protocol + source Field/Method calibration + Store + real Session/ThreadList + named includeinline StackVisitor: compiled drain, shadow-only observation, native/runtime/transition independent boundaries, actual Class relocation, stale declaring/flags/span/generation refusals noalloc/noOS PASS (host receiver/ART leaves; graph completeness/publication not granted)\n";
}

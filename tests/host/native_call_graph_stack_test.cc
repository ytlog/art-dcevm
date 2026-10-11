#include "native_call_graph_stack_fixture.h"
int main(){
 using namespace drain_fixture;std::string error;Environment environment;environment.Prepare(error);
 auto&hooks=environment.hooks;auto&owner_hooks=environment.owner_hooks;auto&metadata=environment.metadata;
 auto&model=environment.model;auto&store=environment.store;auto root=environment.root;
 auto&source=environment.source;auto&threads=environment.threads;
 native_selective_interpretation::Service selection;assert(selection.CalibrateOwner(owner_hooks,&early_owner_fixture::jni,&error));
 native_selective_interpretation::Method selected[]{ {reinterpret_cast<char*>(old_method)+32,root,root,1,1},{reinterpret_cast<char*>(old_method)+64,root,root,2,9} };
 assert(selection.Prepare(owner_hooks,model,metadata,store,selected,2,&error));
 boundary_geometry=true;
 native_call_graph_stack::Service scanner;assert(scanner.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,&error));native_call_graph_stack::Service::Prepared prepared;assert(scanner.Prepare(selection,&prepared,&error));assert(!scanner.Bind(hooks,stack_visitor_service::Declaration::kAndroid11,&error));
 native_selective_interpretation::Service unavailable;native_call_graph_stack::Service::Prepared rejected;assert(!scanner.Prepare(unavailable,&rejected,&error));
 abi::arm64::android12::Method12 runtime_method{};runtime_method.dex_method_index=UINT32_MAX;
 frames[0]={{selected[0].method,false},{selected[1].method,false},{&runtime_method,false},{old_method,false},{nullptr,false}};frames[1]={{selected[1].method,true}};frames[2]={{nullptr,false}};
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(&runtime_method);
 for(const auto&frame:frames[0])maps.Cover(frame.words);modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};
 struct Work{native_call_graph_stack::Service&scanner;native_call_graph_stack::Service::Prepared&prepared;native_selective_interpretation::Service&selection;native_cache::Service&threads;modern_field_metadata::ReadContext&read;ExplicitMappings&maps;int scenario=0;native_call_graph_stack::Report report;}work{scanner,prepared,selection,threads,read,maps,0,{}};
 const auto inspect=[](void*p,native_source_stop::Session&stop)noexcept{auto&w=*static_cast<Work*>(p);native_selective_interpretation::Report activated;
  if(!w.scenario){w.report=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);assert(!w.report.walk_complete&&walk_calls==0);assert(w.selection.Activate(stop,thread.data(),w.read,&activated)&&activated.requires_stack_drain);}
  const auto os=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  w.report=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);
  if(w.report.walk_complete){
   assert(w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));
   const auto*stream=w.scanner.Frames(w.prepared);assert(stream);
   size_t checked_managed=0;
   for(const auto&frame:*stream)if(frame.physical_boundary){
    assert(frame.geometry.size==64&&frame.return_pc==0x123450&&frame.return_slot==static_cast<uintptr_t*>(frame.quick)+7);
    const auto saved=*frame.return_slot;*frame.return_slot^=16;
    assert(!w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));*frame.return_slot=saved;
    assert(w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));
    if(frame.kind==native_call_graph_stack::FrameKind::Managed){
     // A physical managed frame's saved PC must be current, but it supplies
     // no native-return continuation ordinal or native permission by itself.
     assert(frame.carrier_method==frame.method&&frame.return_carrier==SIZE_MAX);
    }
    if(frame.return_carrier!=SIZE_MAX){
     const auto&next=(*stream)[frame.return_carrier];auto*slot=static_cast<void**>(next.quick);
     const auto outer=*slot;*slot=nullptr;
     assert(!w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));*slot=outer;
     assert(w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));
    }
    checked_managed+=frame.kind==native_call_graph_stack::FrameKind::Managed;
   }
   assert(checked_managed==w.report.managed_geometry);
  }
  if(!w.scenario&&w.report.walk_complete){
   // Original continuation addresses outside the frame, inside the method
   // slot, or unaligned publish no physical evidence. The complete walk may
   // still be inspected, but it cannot acquire a JNI continuation grant.
   for(uintptr_t offset:{uintptr_t(64),uintptr_t(0),uintptr_t(57)}){
    return_address_offset=offset;
    const auto refused=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);
    assert(refused.walk_complete&&!refused.managed_geometry&&!refused.native_geometry&&!refused.runtime_geometry);
    const auto*stream=w.scanner.Frames(w.prepared);assert(stream);
    for(const auto&frame:*stream)assert(!frame.physical_boundary&&!frame.return_slot&&!frame.return_pc);
   }
   return_address_offset=56;
   w.report=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);
   assert(w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));
  }
  forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==os);return true;};
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.registered_threads==3&&work.report.visited_threads==3&&work.report.selected_quick==2&&work.report.selected_shadow==1&&work.report.drain_required&&!work.report.selected_drained&&work.report.native_boundaries==1&&work.report.runtime_boundaries==1&&work.report.transitions==2&&work.report.boundary_owners_required&&work.report.managed_geometry==2&&work.report.native_geometry==1&&work.report.runtime_geometry==1);
 frames[0][0].shadow=frames[0][1].shadow=true;work.scenario=1;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.selected_drained&&!work.report.drain_required&&work.report.selected_shadow==3&&work.report.boundary_owners_required);
 // Original quick fragment transition: zero method slot, retained quick
 // cursor and returned PC. The reader must preserve and revalidate all three.
 alignas(16) uintptr_t terminal[8]{};maps.Cover(terminal);
 frames[2][0].carrier=terminal;frames[2][0].pc=0x456780;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.transitions==2);
 struct TransitionCheck {native_call_graph_stack::Service&scanner;native_call_graph_stack::Service::Prepared&prepared;
  native_cache::Service&threads;modern_field_metadata::ReadContext&read;uintptr_t*terminal;};
 TransitionCheck transition{scanner,prepared,threads,read,terminal};
 const auto check_transition=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&r=*static_cast<TransitionCheck*>(raw);
  assert(r.scanner.Scan(stop,thread.data(),r.threads,r.prepared,r.read).walk_complete);
  const auto*stream=r.scanner.Frames(r.prepared);assert(stream);bool found=false;
  for(const auto&frame:*stream)if(frame.kind==native_call_graph_stack::FrameKind::Transition&&frame.quick==r.terminal){
   assert(!frame.method&&!frame.carrier_method&&frame.pc==0x456780);found=true;
  }
  assert(found&&r.scanner.ValidateStream(r.prepared,stop,thread.data(),r.read));
  r.terminal[0]=uintptr_t(old_method);
  assert(!r.scanner.ValidateStream(r.prepared,stop,thread.data(),r.read));
  r.terminal[0]=0;assert(r.scanner.ValidateStream(r.prepared,stop,thread.data(),r.read));
  transition_method_override=old_method;
  const auto rejected=r.scanner.Scan(stop,thread.data(),r.threads,r.prepared,r.read);
  assert(!rejected.walk_complete&&rejected.failure&&r.scanner.Frames(r.prepared)==nullptr);
  transition_method_override=nullptr;
  assert(r.scanner.Scan(stop,thread.data(),r.threads,r.prepared,r.read).walk_complete);return true;
 };
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&transition,check_transition));}
 frames[2][0].carrier=nullptr;frames[2][0].pc=0;
 // Two adjacent physical frames model the original native return carrier.
 // Matching PC alone cannot connect disjoint stack storage, and matching
 // addresses alone cannot connect a different continuation PC.
 alignas(16) uintptr_t carriers[24]{};maps.Cover(carriers);
 frames[0][2].method=old_method;frames[0][2].carrier=carriers;
 frames[0][3].method=&runtime_method;frames[0][3].carrier=carriers+8;frames[0][3].pc=0x123450;
 work.scenario=2;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&work.report.native_continuations==1&&!work.report.runtime_continuations);
 frames[0][3].pc=0x123460;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&!work.report.native_continuations);
 frames[0][3].pc=0x123450;frames[0][3].carrier=carriers+16;
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 assert(work.report.walk_complete&&!work.report.native_continuations);
 frames[0][3].carrier=carriers+8;
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

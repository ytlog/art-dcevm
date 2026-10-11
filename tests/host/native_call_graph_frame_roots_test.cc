// Actual complete walker + SourceStop + Runtime root observer composition.
// ART leaves are boundary doubles; no stop or private commit receipt is forged.
#include "native_call_graph_stack_fixture.h"
namespace {
using namespace drain_fixture;
namespace roots_abi=artdcevm::abi::arm64::runtime_root_visit;
struct RootCursor:abi::arm64::stack::Visitor13 {bool VisitFrame()override{return true;}} native_cursor,shadow_cursor;
int trace_lock_object,class_lock_object;void*trace_slot=&trace_lock_object,*class_slot=&class_lock_object;
void*root_value=nullptr;int root_case=0;size_t original_root_calls=0;
uintptr_t unknown_carrier[8]{};
int32_t RootTid(){return 123;}
int32_t RootLockOwner(const void*p){assert(p==trace_slot||p==class_slot);return 0;}
bool RootShared(const void*p,const void*t){assert(p==class_slot&&t==thread.data());return false;}
void*RootBarrier(void*){std::abort();}
void*RootMethod(const void*raw){assert(stopped);auto*v=static_cast<const abi::arm64::stack::Visitor13*>(raw);
 if(v->kind==stack_visitor_service::WalkKind::kIncludeInlined)return current_method;
 if(raw==&native_cursor&&root_case==5)return frames[1][0].method;
 if(v->shadow_frame)return static_cast<const Frame*>(v->shadow_frame)->method;
 return v->quick_frame?*v->quick_frame:nullptr;
}
void OriginalRoots(void*runtime_owner,roots_abi::Visitor*visitor,uint8_t flags){
 assert(runtime_owner==&::runtime&&stopped&&gc&&flags==roots_abi::kAll);++original_root_calls;
 native_cursor.kind=shadow_cursor.kind=stack_visitor_service::WalkKind::kSkipInlined;
 native_cursor.thread=root_case==1?registered[2]:registered[0];native_cursor.shadow_frame=nullptr;
 native_cursor.quick_frame=reinterpret_cast<void**>(root_case==3?unknown_carrier:frames[0][0].words);
 native_cursor.pc=root_case==2?0x1234:0;
 shadow_cursor.thread=registered[1];shadow_cursor.shadow_frame=&frames[1][0];shadow_cursor.quick_frame=nullptr;shadow_cursor.pc=0;
 // The pointer slot is synchronous ART temporary storage. Deliberately clear
 // it after callbacks; no consumer may retain this address/value as a spill.
 root_value=old_class;void*temporary=root_value;void**slots[]{&temporary};
 roots_abi::JavaFrameInfo native_info{{nullptr,roots_abi::kJavaFrameRoot,19},root_case==8?nullptr:&native_cursor,roots_abi::kNativeReferenceArgumentVreg};
 visitor->VisitRoots(slots,1,native_info.base);
 roots_abi::JavaFrameInfo shadow_info{{nullptr,roots_abi::kJavaFrameRoot,20},&shadow_cursor,
  root_case==4?roots_abi::kNativeReferenceArgumentVreg:size_t(0)};
 visitor->VisitRoots(slots,1,shadow_info.base);
 temporary=nullptr;
 if(root_case==7)frames[0][0].words[0]=0;
}
struct CursorRootHooks:Hooks {
 void*FindSymbol(const char*lib,const char*symbol)override{
#define ROOT_LEAF(name,leaf) if(!std::strcmp(symbol,name))return reinterpret_cast<void*>(leaf)
  ROOT_LEAF(roots_abi::kVisit,OriginalRoots);ROOT_LEAF(roots_abi::kTid,RootTid);
  ROOT_LEAF(roots_abi::kMutexOwner,RootLockOwner);ROOT_LEAF(roots_abi::kRwOwner,RootLockOwner);
  ROOT_LEAF(roots_abi::kShared,RootShared);ROOT_LEAF("artReadBarrierForRootSlow",RootBarrier);
  ROOT_LEAF(stack_visitor_service::Service::MethodSymbol(),RootMethod);
#undef ROOT_LEAF
  if(!std::strcmp(symbol,roots_abi::kTraceLock))return &trace_slot;
  if(!std::strcmp(symbol,roots_abi::kClassLock))return &class_slot;
  return Hooks::FindSymbol(lib,symbol);
 }
};
}
int main(){
 using namespace drain_fixture;Environment environment;std::string error;environment.Prepare(error);
 native_selective_interpretation::Service selection;assert(selection.CalibrateOwner(environment.owner_hooks,&early_owner_fixture::jni,&error));
 native_selective_interpretation::Method selected[]{{reinterpret_cast<char*>(old_method)+32,environment.root,environment.root,1,1}};
 assert(selection.Prepare(environment.owner_hooks,environment.model,environment.metadata,environment.store,selected,1,&error));
 boundary_geometry=true;CursorRootHooks hooks;native_call_graph_stack::Service scanner;
 native_call_graph_stack::Service::Prepared prepared;assert(scanner.Bind(hooks,stack_visitor_service::Declaration::kAndroid13To16,&error)&&scanner.Prepare(selection,&prepared,&error));
 frames[0]={{old_method,false}};frames[1]={{selected[0].method,true}};frames[2]={{nullptr,false}};
 unknown_carrier[0]=uintptr_t(old_method);
 native_typed_heap_walk::RootProvenance roots;assert(roots.Bind(hooks,&::runtime,Current,&error));
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(frames[0][0].words);maps.Cover(&native_cursor);maps.Cover(&shadow_cursor);
 maps.Cover(unknown_carrier);maps.Cover(&trace_slot);maps.Cover(&class_slot);maps.Cover(trace_slot);maps.Cover(class_slot);
 // Capture callback stack bounds ordinarily; no OS query occurs in the stop.
 maps.Cover(reinterpret_cast<const char*>(&maps)-16384);
 modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};
 struct Work {native_call_graph_stack::Service&scanner;native_call_graph_stack::Service::Prepared&prepared;
  native_selective_interpretation::Service&selection;native_cache::Service&threads;native_typed_heap_walk::RootProvenance&roots;
  modern_field_metadata::ReadContext&read;ExplicitMappings&maps;}work{scanner,prepared,selection,environment.threads,roots,read,maps};
 const auto inspect=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&w=*static_cast<Work*>(raw);native_selective_interpretation::Report activation;
  assert(w.selection.Activate(stop,thread.data(),w.read,&activation));
  const auto before=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  const auto walk=w.scanner.Scan(stop,thread.data(),w.threads,w.prepared,w.read);assert(walk.walk_complete&&walk.selected_drained);
  if(root_case==6)w.maps.denied=&native_cursor;
  const auto observed=w.scanner.ObserveFrameRoots(w.prepared,stop,thread.data(),w.read,w.roots);
  w.maps.denied=nullptr;
  if(!root_case){assert(observed.completed&&observed.native_roots==1&&observed.managed_roots==1&&!observed.runtime_roots);}
  else{assert(!observed.completed);if(root_case==7)assert(std::strstr(observed.failure,"changed-after-original-visit"));}
  frames[0][0].words[0]=uintptr_t(old_method);
  assert(w.scanner.ValidateStream(w.prepared,stop,thread.data(),w.read));
  forbid_provider_allocation=false;assert(method_identity::MappingOsQueries()==before);return true;
 };
 for(root_case=0;root_case<9;++root_case){native_source_stop::Session stop(environment.source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 native_source_stop::Session expired(environment.source);const auto before=original_root_calls;
 assert(!scanner.ObserveFrameRoots(prepared,expired,thread.data(),read,roots).completed&&original_root_calls==before);
 environment.store.Clear(&env);assert(!stopped&&!gc&&!locked);
 std::cout<<"Complete walker/Runtime AllRoots same-stop cursor join: native/shadow roots, foreign thread/PC/carrier/method, wrong native argument kind, missing cursor, changed stream and expired receipt refusals noalloc/noOS PASS; no native no-escape or publication permission granted\n";
}

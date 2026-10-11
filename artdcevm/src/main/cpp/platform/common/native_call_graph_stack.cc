#include "platform/common/native_call_graph_stack.h"
#include <algorithm>
#include <functional>
namespace artdcevm::native_call_graph_stack {
namespace {
struct CursorKey {
 void*thread;void*carrier;void*method;uintptr_t pc;bool shadow;
};
CursorKey Key(const Frame&frame)noexcept{
 return {frame.thread,frame.shadow?frame.shadow:frame.quick,
  frame.shadow?frame.method:frame.carrier_method,frame.shadow?0:frame.pc,bool(frame.shadow)};
}
bool KeyLess(const CursorKey&a,const CursorKey&b)noexcept{
 const std::less<void*>less;
 if(a.thread!=b.thread)return less(a.thread,b.thread);
 if(a.carrier!=b.carrier)return less(a.carrier,b.carrier);
 if(a.method!=b.method)return less(a.method,b.method);
 if(a.pc!=b.pc)return a.pc<b.pc;
 return a.shadow<b.shadow;
}
bool IndexedLess(const std::vector<Frame>&frames,size_t a,size_t b)noexcept{
 const auto ka=Key(frames[a]),kb=Key(frames[b]);
 return KeyLess(ka,kb)||(!KeyLess(kb,ka)&&a<b);
}
// Original ARM64 StackVisitor advances a quick cursor by FrameSizeInBytes
// and reads its next PC from GetReturnPcAddr. Join only adjacent callbacks in
// the same thread. Transitions, shadow fragments and missing geometry cannot
// substitute for that physical successor; inline cursors share its carrier.
bool ReturnCarrier(const Frame&boundary,const Frame&next)noexcept{
 return (boundary.kind==FrameKind::Native||boundary.kind==FrameKind::Runtime)&&
  boundary.physical_boundary&&boundary.carrier_method==boundary.method&&boundary.quick&&boundary.geometry.size&&
  boundary.geometry.size<=UINTPTR_MAX-uintptr_t(boundary.quick)&&
  next.thread==boundary.thread&&next.depth>boundary.depth&&!next.shadow&&next.quick&&next.method&&next.carrier_method&&
  next.kind!=FrameKind::Transition&&uintptr_t(next.quick)==uintptr_t(boundary.quick)+boundary.geometry.size&&
  next.pc==boundary.return_pc;
}
}
bool Service::Bind(HookEngine&hooks,stack_visitor_service::Declaration declaration,std::string*error){
 auto fail=[&](const char*s){if(error)*error=s;return false;};
 if(bound_)return fail("call-graph-stack-original-service-already-bound");
 stack_visitor_service::Service stack;
 if(!stack_visitor_service::Service::Bind(declaration,[&](const char*s){return hooks.FindSymbol("libart.so",s);},&stack))return fail("call-graph-stack-original-visitor-unavailable");
 auto create=reinterpret_cast<abi::arm64::context::Create>(hooks.FindSymbol("libart.so",abi::arm64::context::kCreate));
 if(!create)return fail("call-graph-stack-original-context-unavailable");
 geometry_=native_runtime_frame::Service::Bind([&](const char*symbol){return hooks.FindSymbol("libart.so",symbol);});
 stack_=stack;declaration_=declaration;create_=create;bound_=true;return true;
}
bool Service::Reserve(Prepared*out,std::string*error,size_t frame_capacity)const{
 auto fail=[&](const char*s){if(error)*error=s;return false;};
 if(!bound_||!out||!frame_capacity||frame_capacity>out->frames.max_size()||frame_capacity>out->root_index.max_size()||out->selection||(out->service&&out->service!=this))
  return fail("call-graph-stack-scratch-reservation-owner-refused");
 if(out->context)return (out->service==this&&out->frame_limit==frame_capacity)||fail("call-graph-stack-scratch-reservation-owner-or-capacity-changed");
 out->frames.reserve(frame_capacity);out->root_index.reserve(frame_capacity);out->frame_limit=frame_capacity;
 auto*context=create_();if(!context)return fail("call-graph-stack-original-context-creation-failed");
 out->context=context;out->service=this;return true;
}
bool Service::Prepare(const native_selective_interpretation::Service&selection,Prepared*out,std::string*error)const{
 auto fail=[&](const char*s){if(error)*error=s;return false;};
 if(!bound_||!out||out->selection||(out->service&&out->service!=this)||!selection.PreparedSelection())return fail("call-graph-stack-prepared-selection-unavailable");
 std::vector<void*>methods;methods.reserve(selection.SelectedCount());
 for(size_t i=0;i<selection.SelectedCount();++i){void*method=selection.SelectedMethods()[i].method;if(!method)return fail("call-graph-stack-source-selection-missing");methods.push_back(method);}
 std::sort(methods.begin(),methods.end(),std::less<void*>{});
 if(std::adjacent_find(methods.begin(),methods.end())!=methods.end())return fail("call-graph-stack-source-selection-duplicate");
 if(!out->context&&!Reserve(out,error))return false;
 if(out->frames.capacity()<out->frame_limit||out->root_index.capacity()<out->frame_limit||!out->frame_limit)return fail("call-graph-stack-frame-stream-reservation-unavailable");
 out->methods=std::move(methods);out->selection=&selection;return true;
}
bool Service::SelectionSame(const Prepared&p)const noexcept{
 if(!p.selection||p.methods.size()!=p.selection->SelectedCount())return false;
 for(void*m:p.methods)if(!p.selection->Contains(m))return false;return true;
}
const std::vector<Frame>*Service::Frames(const Prepared&p)const noexcept{
 return p.service==this&&p.stream_complete?&p.frames:nullptr;
}
bool Service::ValidateStream(const Prepared&p,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read)const noexcept{
 if(!bound_||p.service!=this||!p.stream_complete||p.stream_stop!=&stop||p.stream_read!=&read||
    p.stream_self!=self||!stop.IsStopped(self)||!p.selection||!SelectionSame(p)||
    p.frames.size()>p.frame_limit||p.root_index.size()!=p.frames.size()||!read.owner||!read.extent)return false;
 // Strict ordering including the walk ordinal proves a full permutation:
 // duplicates, missing entries and stale/out-of-range ordinals are refused.
 for(size_t i=0;i<p.root_index.size();++i){
  if(p.root_index[i]>=p.frames.size()||
     (i&&!IndexedLess(p.frames,p.root_index[i-1],p.root_index[i])))return false;
 }
 for(size_t ordinal=0;ordinal<p.frames.size();++ordinal){const auto&frame=p.frames[ordinal];
  if(!frame.thread||(frame.shadow&&frame.quick))return false;
  if(frame.carrier_method){size_t extent=0;void*current=nullptr;
   if(!frame.quick||frame.shadow||!read.extent(read.owner,frame.quick,&extent)||extent<sizeof(current))return false;
   std::memcpy(&current,frame.quick,sizeof(current));if(current!=frame.carrier_method)return false;
  }
  if(frame.return_carrier!=SIZE_MAX&&
     (frame.return_carrier!=ordinal+1||frame.return_carrier>=p.frames.size()||
      !ReturnCarrier(frame,p.frames[frame.return_carrier])))return false;
  if(frame.kind==FrameKind::Transition){
   if(frame.method||frame.shadow||frame.carrier_method||frame.index!=UINT32_MAX||frame.flags||
      frame.physical_boundary||frame.return_slot||frame.return_pc)return false;
   // Original quick fragments retain their terminal zero method slot and PC.
   // Shadow/empty fragments instead leave the cursor null and PC zero.
   if(frame.quick){
    size_t extent=0;void*terminal=reinterpret_cast<void*>(uintptr_t{1});
    if(uintptr_t(frame.quick)%16||!read.extent(read.owner,frame.quick,&extent)||extent<sizeof(terminal))return false;
    std::memcpy(&terminal,frame.quick,sizeof(terminal));if(terminal)return false;
   }else if(frame.pc)return false;
   continue;
  }
  size_t extent=0;method_metadata::Identity identity{};
  if(!frame.method||!read.extent(read.owner,frame.method,&extent)||
     !method_metadata::ReadIdentity(p.selection->MethodLayout(),frame.method,extent,&identity)||
     identity.dex_method_index!=frame.index||identity.access_flags!=frame.flags)return false;
  const auto kind=identity.dex_method_index==UINT32_MAX?FrameKind::Runtime:
    (identity.access_flags&0x100u)?FrameKind::Native:FrameKind::Managed;
  if(kind!=frame.kind||(kind==FrameKind::Managed&&(!frame.shadow&&!frame.quick)))return false;
  if(frame.physical_boundary){
   if(frame.shadow||!frame.quick||frame.carrier_method!=frame.method||
      uintptr_t(frame.quick)%16||frame.geometry.size<16||frame.geometry.size%16||
      frame.geometry.size>UINTPTR_MAX-uintptr_t(frame.quick)||!frame.return_slot||!frame.return_pc)return false;
   const auto address=uintptr_t(frame.return_slot),base=uintptr_t(frame.quick);
   if(address%alignof(uintptr_t)||address<base+sizeof(void*)||address>base+frame.geometry.size-sizeof(uintptr_t))return false;
   if(!read.extent(read.owner,frame.quick,&extent)||extent<frame.geometry.size)return false;
   void*method=nullptr;uintptr_t pc=0;std::memcpy(&method,frame.quick,sizeof(method));
   std::memcpy(&pc,frame.return_slot,sizeof(pc));
   if(method!=frame.method||pc!=frame.return_pc)return false;
  }
 }
 return stop.IsStopped(self);
}
bool Service::MatchFrameRoot(const Prepared&p,const modern_field_metadata::ReadContext&read,
 const native_typed_heap_walk::RootRecord&root,size_t*out)const noexcept{
 using namespace abi::arm64::runtime_root_visit;
 if(!out||root.type!=kJavaFrameRoot||!root.thread_id||!root.original_frame_visitor||
    p.service!=this||!p.stream_complete||p.stream_read!=&read||!p.stream_stop||
    !p.stream_stop->IsStopped(p.stream_self)||!read.owner||!read.extent)return false;
 const auto*cursor=root.original_frame_visitor;size_t extent=0;
 if(!read.extent(read.owner,cursor,&extent)||extent<stack_visitor_service::Service::StorageSize(declaration_))return false;
 void*method=nullptr,*thread=nullptr,*shadow=nullptr,*quick=nullptr;uintptr_t pc=0;
 if(!stack_.Method(cursor,extent,&method)||!method)return false;
 const auto*bytes=static_cast<const unsigned char*>(cursor);
 std::memcpy(&thread,bytes+abi::arm64::stack::kThreadOffset,sizeof(thread));
 std::memcpy(&shadow,bytes+abi::arm64::stack::kShadowOffset,sizeof(shadow));
 std::memcpy(&quick,bytes+abi::arm64::stack::kQuickOffset,sizeof(quick));
 std::memcpy(&pc,bytes+abi::arm64::stack::kQuickPcOffset,sizeof(pc));
 if(!thread||(shadow&&quick)||(!shadow&&!quick))return false;
 // ReferenceMapVisitor skips inline cursors; match the original outer
 // quick-slot method. Equal physical keys retain the first complete-walk
 // ordinal, including inline cursors, just as the original census ordering.
 const CursorKey key{thread,shadow?shadow:quick,method,shadow?0:pc,bool(shadow)};
 const auto found=std::lower_bound(p.root_index.begin(),p.root_index.end(),key,
  [&](size_t ordinal,const CursorKey&value){return KeyLess(Key(p.frames[ordinal]),value);});
 if(found==p.root_index.end()||KeyLess(key,Key(p.frames[*found])))return false;
 const auto&frame=p.frames[*found];
 if(frame.kind==FrameKind::Transition||
    (root.vreg==kNativeReferenceArgumentVreg&&frame.kind!=FrameKind::Native))return false;
 *out=*found;return p.stream_stop->IsStopped(p.stream_self);
}
FrameRootReport Service::ObserveFrameRoots(const Prepared&p,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read,const native_typed_heap_walk::RootProvenance&roots)const noexcept{
 FrameRootReport report;
 if(!ValidateStream(p,stop,self,read)){report.failure="call-graph-frame-roots-current-stream-refused";return report;}
 struct Sink final:native_typed_heap_walk::RootSink {
  const Service&service;const Prepared&prepared;const modern_field_metadata::ReadContext&read;FrameRootReport&report;
  Sink(const Service&s,const Prepared&p,const modern_field_metadata::ReadContext&r,FrameRootReport&o):service(s),prepared(p),read(r),report(o){}
  bool Root(const native_typed_heap_walk::RootRecord&root)noexcept override{
   if(root.type!=abi::arm64::runtime_root_visit::kJavaFrameRoot)return true;
   size_t ordinal=SIZE_MAX;
   if(!service.MatchFrameRoot(prepared,read,root,&ordinal)||ordinal>=prepared.frames.size()){
    report.failure="call-graph-frame-roots-original-cursor-not-in-complete-stream";return false;}
   switch(prepared.frames[ordinal].kind){
    case FrameKind::Managed:++report.managed_roots;break;
    case FrameKind::Native:++report.native_roots;break;
    case FrameKind::Runtime:++report.runtime_roots;break;
    default:return false;
   }
   return true;
  }
 }sink(*this,p,read,report);
 report.roots=roots.ObserveFrameRoots(stop,self,read,sink);
 if(!report.roots.completed){if(!report.failure)report.failure="call-graph-frame-roots-original-runtime-stream-refused";return report;}
 if(!ValidateStream(p,stop,self,read)){report.failure="call-graph-frame-roots-stream-changed-after-original-visit";return report;}
 report.completed=true;return report;
}
template<class Base>bool Service::Walk(void*target,const native_source_stop::Session&stop,void*self,
 Prepared&prepared,const modern_field_metadata::ReadContext&read,Report&report)const noexcept{
 struct Visitor final:Base {
  const Service&service;const native_source_stop::Session&stop;void*self;void*target;Prepared&prepared;
  const modern_field_metadata::ReadContext&read;Report&report;bool failed=false;
  Visitor(const Service&s,const native_source_stop::Session&o,void*t,void*actual,Prepared&p,const modern_field_metadata::ReadContext&r,Report&v):service(s),stop(o),self(t),target(actual),prepared(p),read(r),report(v){}
  bool VisitFrame()override{
   auto deny=[&](const char*s){failed=true;report.failure=s;return false;};
   if(!stop.IsStopped(self))return deny("call-graph-stack-original-stop-changed");
   if(this->thread!=target||this->context!=prepared.context)return deny("call-graph-stack-original-thread-or-context-cursor-changed");
   void*method=nullptr;if(!service.stack_.Method(this,sizeof(Base),&method))return deny("call-graph-stack-original-method-read-failed");
   if(this->shadow_frame&&this->quick_frame)return deny("call-graph-stack-mixed-managed-carrier");
   const auto record=[&](FrameKind kind,const method_metadata::Identity*identity){
    if(prepared.frames.size()>=prepared.frame_limit||prepared.frames.size()>=prepared.frames.capacity())
     return deny("call-graph-stack-complete-frame-stream-capacity-exhausted");
    prepared.frames.push_back({kind,this->thread,method,this->shadow_frame,this->quick_frame,nullptr,this->pc,this->depth,
      identity?identity->dex_method_index:UINT32_MAX,identity?identity->access_flags:0});
    auto&frame=prepared.frames.back();
    if(frame.quick&&!frame.shadow){size_t extent=0;
     if(read.owner&&read.extent&&read.extent(read.owner,frame.quick,&extent)&&extent>=sizeof(void*))
      std::memcpy(&frame.carrier_method,frame.quick,sizeof(frame.carrier_method));
    }
    if(kind!=FrameKind::Transition&&this->quick_frame&&!this->shadow_frame&&
       frame.carrier_method==method&&service.geometry_.Physical(this,method,this->quick_frame,read,&frame.geometry,&frame.return_slot,&frame.return_pc)){
     frame.physical_boundary=true;
     if(kind==FrameKind::Managed)++report.managed_geometry;
     else if(kind==FrameKind::Native)++report.native_geometry;
     else ++report.runtime_geometry;
    }
    ++report.recorded_frames;return true;
   };
   if(!method){
    if(this->shadow_frame)return deny("call-graph-stack-null-shadow-method");
    if(this->quick_frame){
     size_t extent=0;void*terminal=reinterpret_cast<void*>(uintptr_t{1});
     if(uintptr_t(this->quick_frame)%16||!read.owner||!read.extent||
        !read.extent(read.owner,this->quick_frame,&extent)||extent<sizeof(terminal))
      return deny("call-graph-stack-quick-transition-slot-unreadable");
     std::memcpy(&terminal,this->quick_frame,sizeof(terminal));
     if(terminal)return deny("call-graph-stack-quick-transition-method-is-not-zero");
    }else if(this->pc)return deny("call-graph-stack-empty-transition-PC-is-not-zero");
    if(!record(FrameKind::Transition,nullptr))return false;
    ++report.transitions;report.boundary_owners_required=true;return true;}
   size_t extent=0;method_metadata::Identity identity{};
   if(!read.owner||!read.extent||!read.extent(read.owner,method,&extent)||!method_metadata::ReadIdentity(prepared.selection->MethodLayout(),method,extent,&identity))return deny("call-graph-stack-method-source-span-unproved");
   if(identity.dex_method_index==UINT32_MAX){if(!record(FrameKind::Runtime,&identity))return false;
    ++report.runtime_boundaries;report.boundary_owners_required=true;return true;}
   if(identity.access_flags&0x100u){if(!record(FrameKind::Native,&identity))return false;
    ++report.native_boundaries;report.boundary_owners_required=true;return true;}
   if(identity.access_flags&0x40400u)return deny("call-graph-stack-abstract-or-obsolete-managed-frame");
   if(!this->shadow_frame&&!this->quick_frame)return deny("call-graph-stack-managed-carrier-unavailable");
   if(!record(FrameKind::Managed,&identity))return false;
   ++report.java_frames;
   if(std::binary_search(prepared.methods.begin(),prepared.methods.end(),method,std::less<void*>{})){
    if(this->quick_frame){++report.selected_quick;report.drain_required=true;}
    else ++report.selected_shadow;
   }
   return true;
  }
 }visitor(*this,stop,self,target,prepared,read,report);
 prepared.context->Reset();
 if(!stack_.Construct(&visitor,sizeof(Base),target,prepared.context,stack_visitor_service::WalkKind::kIncludeInlined,true)||!stack_.Walk(&visitor,sizeof(Base),true)){report.failure="call-graph-stack-original-complete-walk-unavailable";return false;}
 return !visitor.failed&&stop.IsStopped(self);
}
Report Service::Scan(const native_source_stop::Session&stop,void*self,const native_cache::Service&threads,
 Prepared&prepared,const modern_field_metadata::ReadContext&read)const noexcept{
 return ScanImpl(stop,self,threads,prepared,read,true);
}
Report Service::ScanForRefund(const native_source_stop::Session&stop,void*self,const native_cache::Service&threads,
 Prepared&prepared,const modern_field_metadata::ReadContext&read)const noexcept{
 return ScanImpl(stop,self,threads,prepared,read,false);
}
Report Service::ScanImpl(const native_source_stop::Session&stop,void*self,const native_cache::Service&threads,
 Prepared&prepared,const modern_field_metadata::ReadContext&read,bool require_retained)const noexcept{
 Report report;prepared.stream_complete=false;prepared.stream_stop=nullptr;
 prepared.stream_read=nullptr;prepared.stream_self=nullptr;prepared.frames.clear();prepared.root_index.clear();
 auto deny=[&](const char*s){report.failure=s;return report;};
 if(!prepared.frame_limit||prepared.frames.capacity()<prepared.frame_limit||
    prepared.root_index.capacity()<prepared.frame_limit)return deny("call-graph-stack-frame-index-reservation-unavailable");
 if(!bound_||prepared.service!=this||!prepared.context||!prepared.selection||!stop.IsStopped(self)||!SelectionSame(prepared))return deny("call-graph-stack-original-stop-or-selection-unproved");
 native_selective_interpretation::Report retained;
 const auto validate=[&](){return require_retained?prepared.selection->ValidateRetained(stop,self,read,&retained):
  prepared.selection->ValidateIdentities(stop,self,read,&retained.failure);};
 if(!validate())return deny(retained.failure?retained.failure:"call-graph-stack-selection-identity-unproved");
 struct Request{const Service&service;const native_source_stop::Session&stop;void*self;Prepared&prepared;const modern_field_metadata::ReadContext&read;Report&report;}request{*this,stop,self,prepared,read,report};
 const auto visit=[](void*thread,void*raw){auto&r=*static_cast<Request*>(raw);++r.report.visited_threads;
  switch(r.service.declaration_){
   case stack_visitor_service::Declaration::kAndroid11:return r.service.Walk<abi::arm64::stack::Visitor11>(thread,r.stop,r.self,r.prepared,r.read,r.report);
   case stack_visitor_service::Declaration::kAndroid12:return r.service.Walk<abi::arm64::stack::Visitor12>(thread,r.stop,r.self,r.prepared,r.read,r.report);
   case stack_visitor_service::Declaration::kAndroid13To16:return r.service.Walk<abi::arm64::stack::Visitor13>(thread,r.stop,r.self,r.prepared,r.read,r.report);
   case stack_visitor_service::Declaration::kAndroid17:return r.service.Walk<abi::arm64::stack::Visitor17>(thread,r.stop,r.self,r.prepared,r.read,r.report);
  }return false;};
 if(!threads.VisitStoppedWorld(visit,&request,&report.registered_threads,nullptr))return deny(report.failure?report.failure:"call-graph-stack-complete-thread-enumeration-failed");
 if(!report.registered_threads||report.registered_threads!=report.visited_threads||!stop.IsStopped(self)||!SelectionSame(prepared))return deny("call-graph-stack-complete-thread-or-selection-changed");
 if(!validate())return deny(retained.failure?retained.failure:"call-graph-stack-final-selection-identity-unproved");
 if(report.recorded_frames!=prepared.frames.size()||
    report.recorded_frames!=report.java_frames+report.native_boundaries+report.runtime_boundaries+report.transitions)
  return deny("call-graph-stack-complete-frame-stream-count-changed");
 // Publish continuation ordinals only after the entire thread census and
 // frame stream succeeded. No partial walk or preceding scan may supply them.
 for(size_t ordinal=0;ordinal+1<prepared.frames.size();++ordinal){auto&frame=prepared.frames[ordinal];
  if(!ReturnCarrier(frame,prepared.frames[ordinal+1]))continue;
  frame.return_carrier=ordinal+1;
  if(frame.kind==FrameKind::Native)++report.native_continuations;
  else if(frame.kind==FrameKind::Runtime)++report.runtime_continuations;
 }
 for(size_t ordinal=0;ordinal<prepared.frames.size();++ordinal)prepared.root_index.push_back(ordinal);
 std::sort(prepared.root_index.begin(),prepared.root_index.end(),
  [&](size_t a,size_t b){return IndexedLess(prepared.frames,a,b);});
 prepared.stream_stop=&stop;prepared.stream_read=&read;prepared.stream_self=self;prepared.stream_complete=true;
 report.walk_complete=true;report.selected_drained=!report.drain_required;
 if(report.drain_required)report.failure="call-graph-stack-selected-compiled-frame-drain-required";
 return report;
}
}

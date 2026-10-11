#include "platform/common/native_call_graph_stack.h"
#include <algorithm>
#include <functional>
namespace artdcevm::native_call_graph_stack {
bool Service::Bind(HookEngine&hooks,stack_visitor_service::Declaration declaration,std::string*error){
 auto fail=[&](const char*s){if(error)*error=s;return false;};
 if(bound_)return fail("call-graph-stack-original-service-already-bound");
 stack_visitor_service::Service stack;
 if(!stack_visitor_service::Service::Bind(declaration,[&](const char*s){return hooks.FindSymbol("libart.so",s);},&stack))return fail("call-graph-stack-original-visitor-unavailable");
 auto create=reinterpret_cast<abi::arm64::context::Create>(hooks.FindSymbol("libart.so",abi::arm64::context::kCreate));
 if(!create)return fail("call-graph-stack-original-context-unavailable");
 stack_=stack;declaration_=declaration;create_=create;bound_=true;return true;
}
bool Service::Prepare(const native_selective_interpretation::Service&selection,Prepared*out,std::string*error)const{
 auto fail=[&](const char*s){if(error)*error=s;return false;};
 if(!bound_||!out||out->service||!selection.SelectedCount())return fail("call-graph-stack-prepared-selection-unavailable");
 std::vector<void*>methods;methods.reserve(selection.SelectedCount());
 for(size_t i=0;i<selection.SelectedCount();++i){void*method=selection.SelectedMethods()[i].method;if(!method)return fail("call-graph-stack-source-selection-missing");methods.push_back(method);}
 std::sort(methods.begin(),methods.end(),std::less<void*>{});
 if(std::adjacent_find(methods.begin(),methods.end())!=methods.end())return fail("call-graph-stack-source-selection-duplicate");
 auto*context=create_();if(!context)return fail("call-graph-stack-original-context-creation-failed");
 out->methods=std::move(methods);out->context=context;out->selection=&selection;out->service=this;return true;
}
bool Service::SelectionSame(const Prepared&p)const noexcept{
 if(!p.selection||p.methods.size()!=p.selection->SelectedCount())return false;
 for(void*m:p.methods)if(!p.selection->Contains(m))return false;return true;
}
template<class Base>bool Service::Walk(void*target,const native_source_stop::Session&stop,void*self,
 Prepared&prepared,const modern_field_metadata::ReadContext&read,Report&report)const noexcept{
 struct Visitor final:Base {
  const Service&service;const native_source_stop::Session&stop;void*self;Prepared&prepared;
  const modern_field_metadata::ReadContext&read;Report&report;bool failed=false;
  Visitor(const Service&s,const native_source_stop::Session&o,void*t,Prepared&p,const modern_field_metadata::ReadContext&r,Report&v):service(s),stop(o),self(t),prepared(p),read(r),report(v){}
  bool VisitFrame()override{
   auto deny=[&](const char*s){failed=true;report.failure=s;return false;};
   if(!stop.IsStopped(self))return deny("call-graph-stack-original-stop-changed");
   void*method=nullptr;if(!service.stack_.Method(this,sizeof(Base),&method))return deny("call-graph-stack-original-method-read-failed");
   if(this->shadow_frame&&this->quick_frame)return deny("call-graph-stack-mixed-managed-carrier");
   if(!method){if(this->shadow_frame||this->quick_frame)return deny("call-graph-stack-null-managed-method");++report.transitions;report.boundary_owners_required=true;return true;}
   size_t extent=0;method_metadata::Identity identity{};
   if(!read.owner||!read.extent||!read.extent(read.owner,method,&extent)||!method_metadata::ReadIdentity(prepared.selection->MethodLayout(),method,extent,&identity))return deny("call-graph-stack-method-source-span-unproved");
   if(identity.dex_method_index==UINT32_MAX){++report.runtime_boundaries;report.boundary_owners_required=true;return true;}
   if(identity.access_flags&0x100u){++report.native_boundaries;report.boundary_owners_required=true;return true;}
   if(identity.access_flags&0x40400u)return deny("call-graph-stack-abstract-or-obsolete-managed-frame");
   if(!this->shadow_frame&&!this->quick_frame)return deny("call-graph-stack-managed-carrier-unavailable");
   ++report.java_frames;
   if(std::binary_search(prepared.methods.begin(),prepared.methods.end(),method,std::less<void*>{})){
    if(this->quick_frame){++report.selected_quick;report.drain_required=true;}
    else ++report.selected_shadow;
   }
   return true;
  }
 }visitor(*this,stop,self,prepared,read,report);
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
 Report report;auto deny=[&](const char*s){report.failure=s;return report;};
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
 report.walk_complete=true;report.selected_drained=!report.drain_required;
 if(report.drain_required)report.failure="call-graph-stack-selected-compiled-frame-drain-required";
 return report;
}
}

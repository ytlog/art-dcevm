#pragma once
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/abi/arm64/context.h"
#include <vector>
namespace artdcevm::native_call_graph_stack {
// Selection-stack facts only. Complete loaded call-graph inventory and all
// native boundary obligations remain independent prerequisites to publication.
struct Report {
 size_t registered_threads=0,visited_threads=0,java_frames=0,selected_quick=0,selected_shadow=0;
 size_t native_boundaries=0,runtime_boundaries=0,transitions=0;
 bool walk_complete=false,drain_required=false,selected_drained=false;
 bool boundary_owners_required=false;const char*failure=nullptr;
};
class Service final {
 public:
 Service()=default;Service(const Service&)=delete;Service&operator=(const Service&)=delete;
 bool Bind(HookEngine&,stack_visitor_service::Declaration,std::string*);
 struct Prepared final {
  Prepared()=default;~Prepared(){delete context;}
  Prepared(const Prepared&)=delete;Prepared&operator=(const Prepared&)=delete;
  private:friend class Service;
  const Service*service=nullptr;const native_selective_interpretation::Service*selection=nullptr;
  std::vector<void*>methods;abi::arm64::context::Prefix*context=nullptr;
 };
 bool Prepare(const native_selective_interpretation::Service&,Prepared*,std::string*)const;
 Report Scan(const native_source_stop::Session&,void*self,const native_cache::Service&,
  Prepared&,const modern_field_metadata::ReadContext&)const noexcept;
 private:
 template<class Base>bool Walk(void*,const native_source_stop::Session&,void*,Prepared&,
  const modern_field_metadata::ReadContext&,Report&)const noexcept;
 bool SelectionSame(const Prepared&)const noexcept;
 stack_visitor_service::Service stack_;stack_visitor_service::Declaration declaration_{};
 abi::arm64::context::Create create_=nullptr;bool bound_=false;
};
}

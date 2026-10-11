#pragma once
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/abi/arm64/context.h"
#include "platform/common/native_runtime_frame.h"
#include "platform/common/native_typed_heap_walk.h"
#include <vector>
namespace artdcevm::native_call_graph_stack {
enum class FrameKind:uint8_t {Managed,Native,Runtime,Transition};
// Ephemeral native cursor facts from the actual complete original walker.
// These do not retain managed roots or certify a native continuation. Consumers
// must join the current same-stop stream to their rooted declaration/guard owners.
struct Frame {
 FrameKind kind=FrameKind::Transition;
 void*thread=nullptr,*method=nullptr,*shadow=nullptr,*quick=nullptr;
 // An inline cursor's logical method may differ from its quick frame's
 // outer method. Retain that native slot identity independently when bounded.
 void*carrier_method=nullptr;
 uintptr_t pc=0;size_t depth=0;
 uint32_t index=UINT32_MAX,flags=0;
 native_runtime_frame::FrameInfo geometry{};
 uintptr_t*return_slot=nullptr;uintptr_t return_pc=0;
 bool physical_boundary=false;
 // Next cursor in this complete walk whose physical stack address and PC
 // match the original boundary's return. This is a carrier fact, including
 // a possible inline cursor; it does not identify a Java callsite or consumer.
 size_t return_carrier=SIZE_MAX;
};
// Selection-stack facts only. Complete loaded call-graph inventory and all
// native boundary obligations remain independent prerequisites to publication.
struct Report {
 size_t registered_threads=0,visited_threads=0,java_frames=0,selected_quick=0,selected_shadow=0;
 size_t native_boundaries=0,runtime_boundaries=0,transitions=0;
 size_t recorded_frames=0,native_geometry=0,runtime_geometry=0;
 size_t native_continuations=0,runtime_continuations=0;
 bool walk_complete=false,drain_required=false,selected_drained=false;
 bool boundary_owners_required=false;const char*failure=nullptr;
};
struct FrameRootReport {
 native_typed_heap_walk::RootReport roots{};
 size_t managed_roots=0,native_roots=0,runtime_roots=0;
 bool completed=false;const char*failure=nullptr;
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
  std::vector<Frame>frames;size_t frame_limit=0;
  const native_source_stop::Session*stream_stop=nullptr;
  const modern_field_metadata::ReadContext*stream_read=nullptr;void*stream_self=nullptr;
  bool stream_complete=false;
 };
 // Allocate original native register scratch before the source loading Run.
 // This carries no selection, stack evidence or stopped-window permission.
 bool Reserve(Prepared*,std::string*,size_t frame_capacity=65536)const;
 bool Prepare(const native_selective_interpretation::Service&,Prepared*,std::string*)const;
 Report Scan(const native_source_stop::Session&,void*self,const native_cache::Service&,
  Prepared&,const modern_field_metadata::ReadContext&)const noexcept;
 // Same complete frame walk and rooted identities, without requiring every
 // selected method to be retained. Partial activation must still be refundable.
 // The refund owner must reject *both* selected quick and shadow carriers.
 Report ScanForRefund(const native_source_stop::Session&,void*self,const native_cache::Service&,
  Prepared&,const modern_field_metadata::ReadContext&)const noexcept;
 // Borrowed diagnostic/source facts only, valid until the next scan. A failed
 // or truncated walk publishes no stream, including a previously valid one.
 // This accessor neither validates frame lifetime nor grants stop/commit rights.
 const std::vector<Frame>*Frames(const Prepared&)const noexcept;
 // Read-only identity recheck of the captured stream in its exact scan window.
 // It does not certify native consumers or replace the complete census/guard
 // owners, and does not infer that an active compiled frame has drained.
 bool ValidateStream(const Prepared&,const native_source_stop::Session&,void*self,
                     const modern_field_metadata::ReadContext&)const noexcept;
 // Join the actual Runtime AllRoots frame cursors to this same-stop complete
 // walk. Callback slots and root values never escape. Completion proves this
 // source stream/cursor correspondence only, not weak-root or native no-escape
 // coverage, a writable physical spill, or a commit capability.
 FrameRootReport ObserveFrameRoots(const Prepared&,const native_source_stop::Session&,void*self,
  const modern_field_metadata::ReadContext&,const native_typed_heap_walk::RootProvenance&)const noexcept;
 private:
 bool MatchFrameRoot(const Prepared&,const modern_field_metadata::ReadContext&,
  const native_typed_heap_walk::RootRecord&,size_t*)const noexcept;
 Report ScanImpl(const native_source_stop::Session&,void*,const native_cache::Service&,
  Prepared&,const modern_field_metadata::ReadContext&,bool retained)const noexcept;
 template<class Base>bool Walk(void*,const native_source_stop::Session&,void*,Prepared&,
  const modern_field_metadata::ReadContext&,Report&)const noexcept;
 bool SelectionSame(const Prepared&)const noexcept;
 native_runtime_frame::Service boundaries_;
 stack_visitor_service::Service stack_;stack_visitor_service::Declaration declaration_{};
 abi::arm64::context::Create create_=nullptr;bool bound_=false;
};
}

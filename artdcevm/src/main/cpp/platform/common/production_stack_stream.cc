#include "platform/common/production_stack_stream.h"
#include "platform/common/source_fixture_interpretation.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/redefinition_gate.h"
namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
struct Request {
 HookEngine&hooks;const MetadataCalibration&metadata;const source_handle_roots::Service&handles;
 const native_source_stop::Service&stop;const native_cache::Service&cache;
 const native_typed_heap_walk::RootProvenance&roots;const native_call_graph_stack::Service&stack;
 native_selective_interpretation::Service&selection;StackStreamObservation observation{};
 std::string*error;
};
bool Observe(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<Request*>(raw);const auto&inputs=r.metadata.Inputs();
 if(!actual.env||actual.env->ExceptionCheck()||actual.vm!=r.metadata.Runtime().receivers.java_vm||
    !runnable_jni::Bridge::BorrowOriginalScope(actual)||!redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm)))
  return Fail(r.error,"source-stack-stream-original-context-unavailable");
 source_fixture_roots::Owner roots(actual,r.handles,r.metadata.Fields());
 std::vector<bounded_managed_roots::Slot>slots;
 if(!roots.Stage({inputs.Anchor()},&slots,r.error)||slots.size()!=1||
    !r.selection.Prepare(r.hooks,r.metadata.Methods(),r.metadata.Fields(),roots.Store(),nullptr,0,r.error))return false;
 // A read-only empty selection reserves the fixed calibrated method identity;
 // it never changes an interpretation entry or asserts that compiled frames drained.
 native_call_graph_stack::Service::Prepared prepared;
 if(!r.stack.Prepare(r.selection,&prepared,r.error))return false;
 struct Window {Request&request;native_call_graph_stack::Service::Prepared&prepared;void*self;};
 Window window{r,prepared,actual.self};
 const bool inspected=source_fixture_interpretation::Inspect(r.stop,actual,roots,&window,
  [](void*raw,native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)noexcept{
   auto&w=*static_cast<Window*>(raw);auto&r=w.request;
   const auto frames=r.stack.ScanForRefund(stop,w.self,r.cache,w.prepared,read);
   r.observation.frames=frames;
   if(!frames.walk_complete||!frames.registered_threads||frames.registered_threads!=frames.visited_threads||
      !frames.recorded_frames||!frames.java_frames||!frames.native_boundaries||frames.drain_required||
      !r.stack.ValidateStream(w.prepared,stop,w.self,read))return false;
   const auto roots=r.stack.ObserveFrameRoots(w.prepared,stop,w.self,read,r.roots);
   r.observation.roots=roots;
   if(!roots.completed||!roots.roots.completed||!roots.managed_roots||
      !r.stack.ValidateStream(w.prepared,stop,w.self,read))return false;
   const auto*stream=r.stack.Frames(w.prepared);if(!stream||stream->size()!=frames.recorded_frames)return false;
   // Independently require that this original callback's actual current thread
   // appears as both native and managed. Other registered threads cannot stand in.
   bool native=false,managed=false;
   for(const auto&frame:*stream)if(frame.thread==w.self){
    native|=frame.kind==native_call_graph_stack::FrameKind::Native;
    managed|=frame.kind==native_call_graph_stack::FrameKind::Managed;
   }
   if(!native||!managed)return false;
   r.observation={frames,roots,true};return true;
  },r.error);
 return inspected&&r.observation.completed&&runnable_jni::Bridge::BorrowOriginalScope(actual)&&
     !actual.env->ExceptionCheck();
}
}
bool ObserveStackStream(HookEngine&hooks,JNIEnv*env,const MetadataCalibration&metadata,
 const source_handle_roots::Service&handles,const native_source_stop::Service&stop,
 const native_cache::Service&cache,const native_typed_heap_walk::RootProvenance&roots,
 const native_call_graph_stack::Service&stack,StackStreamObservation*out,std::string*error){
 if(out)*out={};JavaVM*vm=nullptr;
 if(!out||!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||vm!=metadata.Runtime().receivers.java_vm||
    !redefinition_gate::Owned(vm)||!handles.Calibrated()||!stop.Calibrated()||
    !cache.PreparedForCurrentThread()||!roots.LocksCalibrated())
  return Fail(error,"source-stack-stream-ordinary-owner-or-source-dependencies-unavailable");
 native_selective_interpretation::Service selection;
 if(!selection.CalibrateOwner(hooks,env,error))return false;
 Request request{hooks,metadata,handles,stop,cache,roots,stack,selection,{},error};
 const auto result=runnable_executor::WithOriginalScope(env,metadata.Inputs().Anchor(),Observe,&request);
 if(selection.PendingRefund())std::abort();
 if(result!=runnable_jni::Result::kSuccess||!request.observation.completed||env->ExceptionCheck())
  return Fail(error,request.observation.roots.failure?request.observation.roots.failure:
      request.observation.frames.failure?request.observation.frames.failure:
      "source-stack-stream-complete-walk-or-independent-root-join-not-observed");
 *out=request.observation;return true;
}
}

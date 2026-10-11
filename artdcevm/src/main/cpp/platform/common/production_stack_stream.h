#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/native_call_graph_stack.h"
#include "platform/common/source_handle_roots.h"
namespace artdcevm::production_bootstrap {
// Scalars only. An observed full stream/root join does not retain frame cursors
// or prove native continuations, stack drain, no-escape or transaction safety.
struct StackStreamObservation {
 native_call_graph_stack::Report frames{};
 native_call_graph_stack::FrameRootReport roots{};
 size_t managed_fragments=0,current_thread_transitions=0;
 bool completed=false;
};
bool ObserveStackStream(HookEngine&,JNIEnv*,const MetadataCalibration&,
 const source_handle_roots::Service&,const native_source_stop::Service&,
 const native_cache::Service&,const native_typed_heap_walk::RootProvenance&,
 const native_call_graph_stack::Service&,StackStreamObservation*,std::string*);
}

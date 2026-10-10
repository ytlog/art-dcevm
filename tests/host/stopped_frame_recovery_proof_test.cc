#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#define WHOLE_FRAME_FIXTURE_ONLY
#include "whole_process_frames_test.cc"
#include "platform/common/method_identity_calibration.h"
#pragma clang diagnostic pop
namespace {
int runtime_object;void*runtime_slot=&runtime_object;
bool missing_async=false,old_declaration=false,refuse_async=false,deny_slot=false;unsigned async_calls=0;
bool RuntimeMethod(const void* runtime,void*method,uintptr_t pc){
 assert(gc&&exclusive&&list_locked&&runtime==&runtime_object&&method==&compiled_method&&pc==reinterpret_cast<uintptr_t>(header)+36);
 ++async_calls;return !aot&&!refuse_async; // actual fixed ART: AOT cannot async-deopt
}
bool RuntimePc(const void*runtime,uintptr_t pc){return RuntimeMethod(runtime,&compiled_method,pc);}
const void*MethodHeader(void*m,uintptr_t pc){return m==&compiled_method&&pc==reinterpret_cast<uintptr_t>(header)+36?header:nullptr;}
struct RecoveryHooks:FrameHooks {
 void*FindSymbol(const char*lib,const char*name)override {
  if(!std::strcmp(name,native_frame_async_recovery::Service::kRuntime))return &runtime_slot;
  if(!std::strcmp(name,native_frame_async_recovery::Service::kMethod))return missing_async||old_declaration?nullptr:Fn(RuntimeMethod);
  if(!std::strcmp(name,native_frame_async_recovery::Service::kPc))return !missing_async&&old_declaration?Fn(RuntimePc):nullptr;
  if(!std::strcmp(name,native_frame_code_owner::Service::kMethodHeader))return Fn(MethodHeader);
  return FrameHooks::FindSymbol(lib,name);
 }
};
bool Extent(void*owner,const void*value,size_t*out)noexcept {
 auto*session=static_cast<native_source_stop::Session*>(owner);
 if(deny_slot||!session->IsStopped(&self))return false;
 if(value==&runtime_slot){*out=sizeof(runtime_slot);return true;}
 if(value==&runtime_method){*out=sizeof(runtime_method);return true;}
 const auto address=uintptr_t(value),begin=uintptr_t(runtime_stack);
 if(address>=begin&&address<begin+sizeof(runtime_stack)){*out=begin+sizeof(runtime_stack)-address;return true;}
 struct Region {const void*data;size_t bytes;};
 const Region regions[]={{stack_row.data(),sizeof(stack_row)},{inline_row.data(),sizeof(inline_row)},
  {&dexmask,sizeof(dexmask)},{dexmaps,sizeof(dexmaps)},{catalog,sizeof(catalog)},{&quick,sizeof(quick)},{header,sizeof(header)}};
 for(const auto&region:regions){const auto first=uintptr_t(region.data);if(address>=first&&address-first<region.bytes){*out=region.bytes-(address-first);return true;}}
 return false;
}
}
int main(){
 RecoveryHooks hooks;std::string error;assert(native_source_stop::Install(hooks,&error));
 native_cache::Service cache;assert(cache.Bind(hooks,&thread_list,&error)&&cache.Prepare(&error));
 native_source_stop::Service stop;auto states=State(hooks);assert(native_source_stop::Service::Bind(hooks,&heap,states,&stop,&error));Calibrate(stop);self.flags=0;
 source_commit::Plan plan;plan.mirror=&runtime_object;plan.old_classes={&compiled_method};plan.new_classes={&shadow_method};
 missing_async=true;
 {whole_process_frames::Service absent;assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid14,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&absent));whole_process_frames::Service::Prepared storage;assert(absent.Prepare(&storage)&&absent.PrepareRecoveryPlan(plan,&storage));native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));modern_field_metadata::ReadContext context{&owner,Extent};auto report=absent.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context);assert(!report.accepted&&!std::strcmp(report.failure,"whole-frame-original-async-recovery-reader-unavailable"));}
 missing_async=false;
 for(bool old:{false,true}){
  old_declaration=old;whole_process_frames::Service scanner;
  assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid14,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&scanner));
  whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage)&&scanner.PrepareRecoveryPlan(plan,&storage));
  whole_process_frames::StoppedRecoveryProof proof;
  {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));modern_field_metadata::ReadContext context{&owner,Extent};
   runtime_method.dex_method_index=UINT32_MAX;runtime_frame=true;
   const auto queries=method_identity::MappingOsQueries();forbid_native_allocation=true;async_calls=0;
   aot=true;auto census=scanner.CensusRecovery(owner,&self,cache,storage,context);
   assert(census.accepted&&census.frames>=3&&census.slots==0&&async_calls==0);aot=false;
   auto report=scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context,&proof);
   assert(report.accepted&&report.registered_threads==3&&report.walked_threads==3&&report.async_frames==1&&report.runtime_frames==1&&async_calls==2&&proof.Validate(plan));
   source_commit::Plan other;assert(!proof.Validate(other));
   plan.old_classes[0]=&native_method;assert(!proof.Validate(plan));plan.old_classes[0]=&compiled_method;
   refuse_async=true;assert(!proof.Validate(plan));refuse_async=false;
   deny_slot=true;assert(!proof.Validate(plan));deny_slot=false;
   runtime_slot=nullptr;assert(!proof.Validate(plan));runtime_slot=&runtime_object;
   include_self=false;assert(!proof.Validate(plan));include_self=true;
   wrong_native_map=true;assert(!proof.Validate(plan));wrong_native_map=false;
   aot=true;assert(!proof.Validate(plan));whole_process_frames::StoppedRecoveryProof refused;
   report=scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context,&refused);
   assert(!report.accepted&&report.aot_frames==1&&!refused.Validate(plan)&&!std::strcmp(report.failure,"whole-AOT-requires-actual-frame-exit-or-dependency-closure"));aot=false;
   whole_process_frames::StoppedRecoveryProof fresh;assert(scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context,&fresh).accepted);
   assert(!proof.Validate(plan)&&fresh.Validate(plan));proof=fresh;
   assert(method_identity::MappingOsQueries()==queries);forbid_native_allocation=false;runtime_frame=false;
   assert(!proof.ValidateFinal(plan));assert(owner.RollbackBeforeSource());assert(!proof.Validate(plan));
  }
  proof={};assert(!gc&&!exclusive&&!list_locked);
  // Actual resumed AOT frame exit, then a fresh exhaustive stopped scan. The
  // absence is observed by original walker, never asserted from loader/method ID.
  aot=true;{native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));modern_field_metadata::ReadContext context{&owner,Extent};assert(!scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context).accepted);}aot=false;
  interpreter_only=true;
  {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));modern_field_metadata::ReadContext context{&owner,Extent};forbid_native_allocation=true;
   auto report=scanner.ScanRecovery(owner,&self,cache,nullptr,storage,plan,context,&proof);assert(report.accepted&&report.compiled_frames==0&&report.registered_threads==3&&proof.Validate(plan));forbid_native_allocation=false;}
  interpreter_only=false;proof={};
  {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));modern_field_metadata::ReadContext context{&owner,Extent};
   assert(scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context,&proof).accepted);
   assert(owner.ContinueSource());assert(!proof.Validate(plan));disable_proxy(&heap,&self);Native(&self);unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);
   assert(!proof.ValidateFinal(plan));forbid_native_allocation=true;const auto queries=method_identity::MappingOsQueries();
   runtime_frame=true;
   whole_process_frames::StoppedRecoveryProof final_proof;assert(scanner.ScanRecovery(owner,&self,cache,&jit_cache,storage,plan,context,&final_proof).accepted&&final_proof.ValidateFinal(plan));runtime_frame=false;
   assert(method_identity::MappingOsQueries()==queries);forbid_native_allocation=false;resume_proxy(&carrier);assert(!final_proof.ValidateFinal(plan));Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed());}
  proof={};
 }
 std::cout<<"Actual whole threadlist/walk/maps + exact Plan/window proof + original one/two-arg async callee + AOT real resume/exit/fresh scan + expiry/refusal noalloc/noOS PASS\n";
}

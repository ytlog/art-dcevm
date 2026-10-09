#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "waiting_scope_fixture.inc"
#include "compiled_frame_proof_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/compiled_frame_ordinary_context.h"
#include "platform/common/method_identity_calibration.h"
namespace {
struct Inspection {
 frame_family_calibration::Service service;
 frame_family_calibration::Service::Prepared prepared;
 abi::arm64::android12::Method12 metadata[3]{};
 frame_family_calibration::MethodOracle methods[3]{};
 Witness witnesses[3][2]{};
 active_frame_recovery_proof::PhysicalOracle physical;
 decoded_frame_recovery::Location scratch[2];
 frame_family_calibration::Result result;
 unsigned calls=0;
 Inspection(){
  family=Family::kAndroid13;Reset();actual_physical=true;
  const uint32_t modifiers[]{0x101,9,1};
  for(unsigned i=0;i<3;++i){
   physical_quick[i]={};void* method=&method_ids[i];std::memcpy(physical_quick[i].data(),&method,sizeof(method));
   physical_quick[i][2]=quick[2+2*i];physical_quick[i][3]=quick[3+2*i];
   witnesses[i][0]={0,Kind::kInt,quick[2+2*i]};witnesses[i][1]={1,Kind::kReference,quick[3+2*i]};
   physical.callers[i]={method,2,witnesses[i],2,10+i};
   metadata[i].access_flags=modifiers[i];metadata[i].dex_method_index=100+i;
   methods[i]={&metadata[i],sizeof(metadata[i]),100+i,modifiers[i]};
  }
  catalog[0]=1;catalog[1]=3;catalog[2]=1;catalog[3]=4;maps[0]=1;maps[1]=2;
  assert(frame_family_calibration::Service::Bind(ExactFinder{},&service)&&service.Prepare(&prepared));
 }
 static bool Scan(void*o,native_source_stop::Session&stop)noexcept{
  auto& r=*static_cast<Inspection*>(o);++r.calls;
  assert(waiting_fixture::flags==74u<<16&&waiting_fixture::gc&&waiting_fixture::exclusive&&!waiting_fixture::gc_lock);
  assert(stop.IsStopped(&waiting_fixture::flags)&&stop.IsCollectedStop(&waiting_fixture::flags)&&!stop.IsFinalWindow(&waiting_fixture::flags));
  return r.service.Calibrate(&thread,&jit,r.methods,3,r.physical,r.scratch,2,Readable,r.prepared,&r.result);
 }
};
}
int main(){
 namespace w=waiting_fixture;w::Hooks hooks;std::string error;
 assert(native_source_stop::Install(hooks,&error)&&native_source_stop::InstallOriginalWaitingScope(hooks,&error));
 thread_suspend_service::Service states;
 assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kLegacy16,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&states));
 native_source_stop::Service source;
 assert(native_source_stop::Service::Bind(hooks,&w::heap,states,&source,&error));w::flags=67u<<16;
 assert(source.CalibrateOriginalWaiting(&w::flags,sizeof(w::flags),&error));
 native_source_stop::Calibration proof;
 {native_source_stop::Observation observe(source,&w::self_driver,&w::body,&w::flags,sizeof(w::flags));
  assert(observe.Collected(&w::self_driver,&w::holder));w::disable_proxy(&w::heap,&w::flags);w::Native();unsigned char final;
  w::stop_proxy(&final,w::s::kFinalCause,true);w::resume_proxy(&final);w::Runnable();w::enable_proxy(&w::heap,&w::flags);
  assert(observe.Finish(&w::self_driver,true,&proof));}
 assert(source.Authorize(proof,&w::body)&&source.RequiresOriginalWaiting());
 // Fresh ordinary frame calibration has no schema graph-stage Thread field.
 auto*env=reinterpret_cast<JNIEnv*>(&w::holder);auto*vm=reinterpret_cast<JavaVM*>(&w::heap);
 runnable_jni::Context context{&w::flags,env,vm};
 assert(compiled_frame_fixture::SameOrdinaryOwner(context,env,vm,w::Current));
 assert(!compiled_frame_fixture::SameOrdinaryOwner(context,env,vm,nullptr));
 auto wrong=context;wrong.self=&w::body;assert(!compiled_frame_fixture::SameOrdinaryOwner(wrong,env,vm,w::Current));
 wrong=context;wrong.env=nullptr;assert(!compiled_frame_fixture::SameOrdinaryOwner(wrong,env,vm,w::Current));
 wrong=context;wrong.vm=nullptr;assert(!compiled_frame_fixture::SameOrdinaryOwner(wrong,env,vm,w::Current));
 for(bool mismatch:{false,true,false}){
  Inspection inspection;native_map_mismatch=mismatch;native_source_stop::Session stopped(source);
  const auto allocations=creates;const auto mapping_queries=method_identity::MappingOsQueries();noalloc=true;
  const bool accepted=stopped.InspectOriginalWaiting(&w::flags,sizeof(w::flags),&inspection,Inspection::Scan);
  noalloc=false;
  assert(accepted==!mismatch&&inspection.calls==1&&inspection.result.compiled_ready==!mismatch);
  assert(!stopped.Reserved()&&!stopped.IsStopped(&w::flags)&&w::flags==67u<<16&&!w::gc&&!w::exclusive&&!w::gc_lock);
  assert(creates==allocations&&method_identity::MappingOsQueries()==mapping_queries&&w::starts==w::finishes&&w::stops==w::resumes);
  if(accepted)assert(inspection.result.proof.frames==3&&inspection.result.proof.references==3&&inspection.result.proof.integers==3);
 }
 std::cout<<"Actual WaitingGC local compiled CodeInfo/value proof + mismatch refusal + repeated restored ordinary owner PASS\n";
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "waiting_scope_fixture.inc"
#define main nterp_proof_regression_main
#include "native_nterp_frame_proof_test.cc"
#undef main
#pragma clang diagnostic pop
#include "platform/common/method_identity_calibration.h"
namespace {
struct Inspection {
 p::Service reader;native_source_stop::Session*active=nullptr;
 std::array<p::Frame,3> frames;n::Witness witnesses[3][2];p::Report report;unsigned calls=0;
 Inspection(){declaration=p::Declaration::kAndroid13To16;
  assert(p::Service::Bind(declaration,n::CanonicalDeclaration::WithFullRegisterList,Finder{},&reader));
  for(unsigned i=0;i<3;++i){*Frame(i)=&methods[i];uint32_t integer=73+i,reference=0x11000+i*0x1000;
   std::memcpy(reinterpret_cast<void*>(Regs(Frame(i))),&integer,4);std::memcpy(reinterpret_cast<void*>(Refs(Frame(i))+4),&reference,4);std::memcpy(reinterpret_cast<void*>(Regs(Frame(i))+4),&reference,4);
   witnesses[i][0]={0,n::Kind::kInt,integer};witnesses[i][1]={1,n::Kind::kReference,reference};frames[i]={{&methods[i],3,3,19+i},witnesses[i],2};}
 }
 static bool Extent(void*o,const void*value,size_t*out)noexcept {
  auto&r=*static_cast<Inspection*>(o);return r.active&&r.active->IsCollectedStop(&waiting_fixture::flags)&&Range(&target,value,out);
 }
 static bool Scan(void*o,native_source_stop::Session&scope)noexcept {
  auto&r=*static_cast<Inspection*>(o);++r.calls;r.active=&scope;
  assert(waiting_fixture::flags==74u<<16&&waiting_fixture::gc&&waiting_fixture::exclusive&&!waiting_fixture::gc_lock);
  r.report=r.reader.Prove(&target,&context,r.frames,{&r,Extent});r.active=nullptr;return r.report.accepted;
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
 for(unsigned pass=0;pass<3;++pass){
  Inspection inspect;native_source_stop::Session scope(source);
  if(pass){uint32_t fresh=0x21000;std::memcpy(reinterpret_cast<void*>(Refs(Frame(0))+4),&fresh,4);std::memcpy(reinterpret_cast<void*>(Regs(Frame(0))+4),&fresh,4);if(pass==2)inspect.witnesses[0][1].expected=fresh;}
  const auto queries=method_identity::MappingOsQueries();noalloc=true;
  const bool result=scope.InspectOriginalWaiting(&w::flags,sizeof(w::flags),&inspect,Inspection::Scan);noalloc=false;
  assert(result==(pass!=1)&&inspect.calls==1);
  assert(!scope.Reserved()&&!scope.IsStopped(&w::flags)&&w::flags==67u<<16&&!w::gc&&!w::exclusive&&!w::gc_lock);
  assert(queries==method_identity::MappingOsQueries()&&w::starts==w::finishes&&w::stops==w::resumes);
  size_t extent=0;assert(!Inspection::Extent(&inspect,Frame(0),&extent));
  if(result)assert(inspect.report.frames==3&&inspect.report.mask==7&&inspect.report.references==3&&inspect.report.integers==3);
 }
 std::cout<<"Actual WaitingGC owner + original three-physical Nterp stack/canonical array/value proof + stale/refreshed GC root + automatic resume, no native allocation/no OS mapping PASS\n";
}

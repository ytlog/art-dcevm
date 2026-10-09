#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "waiting_scope_fixture.inc"
#define main nterp_leaf_regression_main
#include "native_nterp_frame_reader_test.cc"
#undef main
#pragma clang diagnostic pop
#include "platform/common/method_identity_calibration.h"
namespace {
struct Inspection {
 n::Service reader;
 native_source_stop::Session* active=nullptr;
 n::Oracle oracle{&method,3,3,19};
 n::Witness witnesses[2]{{0,n::Kind::kInt,73},{1,n::Kind::kReference,0x11000}};
 n::Snapshot snapshot;
 bool stale=false;
 unsigned calls=0;
 Inspection(){assert(n::Service::Bind(n::CanonicalDeclaration::WithFullRegisterList,Finder{},&reader));}
 static bool Range(void*o,const void*p,size_t*out)noexcept {
  auto&r=*static_cast<Inspection*>(o);
  if(!r.active||!r.active->IsCollectedStop(&waiting_fixture::flags))return false;
  return Extent(&stopped,p,out);
 }
 static bool Scan(void*o,native_source_stop::Session&scope)noexcept {
  auto&r=*static_cast<Inspection*>(o);++r.calls;r.active=&scope;
  assert(waiting_fixture::flags==74u<<16&&waiting_fixture::gc&&waiting_fixture::exclusive&&!waiting_fixture::gc_lock);
  assert(scope.IsCollectedStop(&waiting_fixture::flags)&&!scope.IsFinalWindow(&waiting_fixture::flags));
  arrays_read=0;
  const bool result=r.reader.Prove(&visitor,Frame(),&header,101,r.oracle,r.witnesses,2,{&r,Range},&r.snapshot);
  r.active=nullptr;return result;
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
 *Frame()=&method;uint32_t integer=73,root=0x11000;
 std::memcpy(reinterpret_cast<void*>(Base()+52),&integer,4);std::memcpy(reinterpret_cast<void*>(Base()+44),&root,4);std::memcpy(reinterpret_cast<void*>(Base()+56),&root,4);
 for(unsigned pass=0;pass<3;++pass){
  Inspection inspect;native_source_stop::Session scope(source);
  if(pass){root=0x12000;std::memcpy(reinterpret_cast<void*>(Base()+44),&root,4);std::memcpy(reinterpret_cast<void*>(Base()+56),&root,4);}
  if(pass==2)inspect.witnesses[1].expected=root;
  const auto queries=method_identity::MappingOsQueries();noalloc=true;
  const bool result=scope.InspectOriginalWaiting(&w::flags,sizeof(w::flags),&inspect,Inspection::Scan);
  noalloc=false;
  assert(result==(pass!=1)&&inspect.calls==1);
  assert(!scope.Reserved()&&!scope.IsStopped(&w::flags)&&w::flags==67u<<16&&!w::gc&&!w::exclusive&&!w::gc_lock);
  assert(queries==method_identity::MappingOsQueries()&&w::starts==w::finishes&&w::stops==w::resumes);
  size_t extent=0;assert(!Inspection::Range(&inspect,Frame(),&extent));
 }
 std::cout<<"Actual WaitingGC owner + independent physical Nterp scalar/reference proof + stale/refreshed GC root + automatic resume, no native allocation/no OS mapping PASS\n";
}

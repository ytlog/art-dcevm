#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wreturn-type"
#define main original_frame_test_main
#include "active_frame_recovery_proof_test.cc"
#undef main
#pragma clang diagnostic pop
namespace {
bool wrong_header=false,ownership_drift=false;unsigned header_observations=0;
const void* AotHeader(void*method,uintptr_t pc){
 ++header_observations;
 if(wrong_header||(ownership_drift&&header_observations>1)||method!=active_method||!HeaderContains(header,pc))return nullptr;
 return header;
}
bool NotJit(const void*j,const void*){assert(j==&jit);return false;}
struct AotFinder {
 bool missing_header=false;
 void* operator()(const char*n)const {
  if(!std::strcmp(n,native_frame_code_owner::Service::kMethodHeader))return missing_header?nullptr:reinterpret_cast<void*>(AotHeader);
  if(!std::strcmp(n,native_frame_code_owner::Service::kJit))return reinterpret_cast<void*>(NotJit);
  return Finder{}(n);
 }
};
}
int main(){
 for(auto f:{Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid15,Family::kAndroid16,Family::kAndroid17}){
  family=f;Reset();actual_physical=true;Witness witnesses[3][2];PhysicalOracle oracle;oracle.code=CodeKind::Aot;
  for(unsigned i=0;i<3;++i){physical_quick[i]={};void*method=&method_ids[i];std::memcpy(physical_quick[i].data(),&method,sizeof(method));
   physical_quick[i][2]=quick[2+2*i];physical_quick[i][3]=quick[3+2*i];
   witnesses[i][0]={0,Kind::kInt,quick[2+2*i]};witnesses[i][1]={1,Kind::kReference,quick[3+2*i]};
   oracle.callers[i]={method,2,witnesses[i],2,10+i};}
  catalog[0]=1;catalog[1]=3;catalog[2]=1;catalog[3]=4;maps[0]=1;maps[1]=2;
  Service service;assert(Service::Bind(f,AotFinder{},&service));decoded_frame_recovery::Location scratch[2];
  auto run=[&]{header_observations=0;return service.ProvePhysical(&thread,&jit,oracle,scratch,2);};
  auto report=run();assert(report.failure==Failure::kNone&&report.aot_frames==3&&report.references==3&&report.integers==3&&header_observations==6);
  header_observations=0;assert(service.ProvePhysical(&thread,nullptr,oracle,scratch,2).failure==Failure::kNone);
  wrong_header=true;assert(run().failure==Failure::kCodeOwnership);wrong_header=false;
  ownership_drift=true;assert(run().failure==Failure::kCodeOwnership);ownership_drift=false;
  native_map_mismatch=true;assert(run().failure==Failure::kDecodedMap);native_map_mismatch=false;
  nterp_header=true;assert(run().failure==Failure::kCodeOwnership);nterp_header=false;
  ++witnesses[1][1].expected;assert(run().failure==Failure::kValueMismatch);--witnesses[1][1].expected;
  // A GC-relocated witness must be refreshed to the actual stopped root value.
  for(unsigned i=0;i<3;++i){quick[3+2*i]+=0x4000;physical_quick[i][3]=quick[3+2*i];}
  assert(run().failure==Failure::kValueMismatch);
  for(unsigned i=0;i<3;++i)witnesses[i][1].expected=quick[3+2*i];assert(run().failure==Failure::kNone);
  Service missing;assert(Service::Bind(f,AotFinder{true},&missing));assert(missing.ProvePhysical(&thread,&jit,oracle,scratch,2).failure==Failure::kCodeOwnership);
  oracle.code=CodeKind::Jit;assert(run().failure==Failure::kCodeOwnership);
 }
 std::puts("Actual AOT exact method/header/PC + physical CodeInfo/native maps/values and refreshed GC witnesses: seven declarations PASS");
}

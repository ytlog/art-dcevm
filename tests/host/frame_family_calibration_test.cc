// Reuse the actual named-native mock implementations and complete CodeInfo
// physical-frame fixtures; selection is exercised through production Service.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main existing_active_frame_proof_test_main
#include "active_frame_recovery_proof_test.cc"
#undef main
#pragma clang diagnostic pop
#include "platform/common/frame_family_calibration.h"
namespace {
struct ExactFinder {
 bool ambiguous=false;
 void* operator()(const char* name)const {
  if(!ambiguous&&std::strcmp(name,abi::arm64::stack::kWalk17)==0&&family!=Family::kAndroid17)return nullptr;
  if(!ambiguous&&std::strcmp(name,abi::arm64::stack::kWalk)==0&&family==Family::kAndroid17)return nullptr;
  if(family!=Family::kAndroid11&&std::strcmp(name,abi::arm64::android11::kFindStackMap)==0)return nullptr;
  if(family==Family::kAndroid11&&std::strcmp(name,abi::arm64::android13::kFindStackMap)==0)return nullptr;
  return Finder{}(name);
 }
};
bool Readable(const void* p,size_t* n) {
 const struct Region {const void* p;size_t n;} regions[]={
  {header,sizeof(header)},{stack.data(),sizeof(stack)},{inlines.data(),sizeof(inlines)},
  {&mask,sizeof(mask)},{&roots,sizeof(roots)},{maps.data(),sizeof(maps)},
  {catalog.data(),sizeof(catalog)}};
 for(const auto& r:regions)if(p==r.p){*n=r.n;return true;}return false;
}
void CheckFamily(Family f){
 family=f;Reset();actual_physical=true;
 Witness witness[3][2];active_frame_recovery_proof::PhysicalOracle physical;
 for(unsigned i=0;i<3;++i){
  physical_quick[i]={};void* owner=&method_ids[i];std::memcpy(physical_quick[i].data(),&owner,sizeof(owner));
  physical_quick[i][2]=quick[2+2*i];physical_quick[i][3]=quick[3+2*i];
  witness[i][0]={0,Kind::kInt,quick[2+2*i]};witness[i][1]={1,Kind::kReference,quick[3+2*i]};
  physical.callers[i]={owner,2,witness[i],2,10+i};
 }
 catalog[0]=1;catalog[1]=3;catalog[2]=1;catalog[3]=4;maps[0]=1;maps[1]=2;
 abi::arm64::android11::Method11 m11[3]{};abi::arm64::android12::Method12 m12[3]{};
 frame_family_calibration::MethodOracle methods[3];
 const uint32_t flags[]={0x101,9,1};
 for(unsigned i=0;i<3;++i){m11[i].access_flags=m12[i].access_flags=flags[i];m11[i].dex_method_index=m12[i].dex_method_index=100+i;
  methods[i]={f==Family::kAndroid11?static_cast<const void*>(&m11[i]):static_cast<const void*>(&m12[i]),f==Family::kAndroid11?sizeof(m11[i]):sizeof(m12[i]),100+i,flags[i]};}
 frame_family_calibration::Service service;assert(frame_family_calibration::Service::Bind(ExactFinder{},&service));
 frame_family_calibration::Service::Prepared prepared;assert(service.Prepare(&prepared));assert(!service.Prepare(&prepared));
 const unsigned allocations=creates;decoded_frame_recovery::Location scratch[2];
 frame_family_calibration::Result result;auto run=[&]{return service.Calibrate(&thread,&jit,methods,3,physical,scratch,2,Readable,prepared,&result);};
 bad_owner=true;
 assert(service.ObserveGeometry(&thread,methods,3,prepared,&result)&&!result.compiled_ready);
 assert(!run());bad_owner=false;
 assert(run()&&result.compiled_ready);assert(creates==allocations&&destroys==0);
 assert(result.proof.frames==3&&result.proof.references==3&&result.proof.integers==3);
 assert(result.scanner_family==(f==Family::kAndroid11?f:f==Family::kAndroid12?f:f==Family::kAndroid17?f:Family::kAndroid13));
 bad_owner=true;assert(!run());assert(!result.compiled_ready&&result.proof.failure!=active_frame_recovery_proof::Failure::kNone);
 assert(result.failure==result.proof.detail);bad_owner=false;
 bad_pc=true;assert(!run());bad_pc=false;
 ++witness[2][1].expected;assert(!run());--witness[2][1].expected;
 native_map_mismatch=true;assert(!run());native_map_mismatch=false;
 const auto saved=methods[0].java_modifiers;methods[0].java_modifiers=1;assert(!run()&&!result.compiled_ready);methods[0].java_modifiers=saved;
 result.compiled_ready=true;assert(!service.ObserveGeometry(nullptr,methods,3,prepared,&result)&&!result.compiled_ready);
 assert(std::strcmp(result.failure,"missing-independent-geometry-inputs")==0);
 ++methods[1].dex_index;assert(!run());--methods[1].dex_index;
 assert(!service.Calibrate(&thread,&jit,methods,3,physical,scratch,2,Unmapped,prepared,&result));
 frame_family_calibration::Service ambiguous;assert(frame_family_calibration::Service::Bind(ExactFinder{true},&ambiguous));
 assert(!ambiguous.Calibrate(&thread,&jit,methods,3,physical,scratch,2,Readable,prepared,&result));
 if(f==Family::kAndroid11){wrong_geometry=true;frame_family_calibration::Service wrong;assert(frame_family_calibration::Service::Bind(ExactFinder{},&wrong));assert(!wrong.Calibrate(&thread,&jit,methods,3,physical,scratch,2,Readable,prepared,&result));wrong_geometry=false;}
 assert(creates==allocations&&destroys==0);
}
}
int main(){for(auto f:{Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid15,Family::kAndroid16,Family::kAndroid17})CheckFamily(f);std::puts("Actual native constructor geometry and complete physical maps/value proofs: seven declarations; wrong prefix, ambiguous symbols, wrong geometry, unowned code, stale DexPc, map/value/mapping denial rejected; preallocated Context preserved");}

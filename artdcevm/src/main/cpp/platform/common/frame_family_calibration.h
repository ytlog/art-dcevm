#pragma once
#include "platform/common/active_frame_recovery_proof.h"
#include "platform/common/art_method_metadata.h"
#include <array>
namespace artdcevm::frame_family_calibration {
using Family=code_info_view::SourceFamily;
// Source geometry, deliberately not an Android SDK or vendor identity.
enum class Geometry {kLegacy368,kLegacy376,kCompact344,kModern368};
struct MethodOracle {const void* method;size_t extent;uint32_t dex_index,java_modifiers;};
struct Result {
 Geometry geometry=Geometry::kCompact344;
 // kAndroid13 expresses the common 13--16 scanner storage/reader declaration;
 // it does not authorize their different InstrumentThreadStack semantics.
 Family scanner_family=Family::kAndroid13;
 active_frame_recovery_proof::Report proof{};
 const char* failure="not-calibrated";
};
class Service {
 public:
 struct Prepared {
  std::unique_ptr<abi::arm64::context::Prefix> context;
  Prepared()=default;Prepared(const Prepared&)=delete;
 };
 // Bind and Prepare while ordinary/runnable, before entering the actual stop.
 template<class Finder> static bool Bind(Finder finder,Service* out) {
  if(!out)return false;Service s;
  s.construct_=reinterpret_cast<abi::arm64::stack::Construct>(finder(abi::arm64::stack::kConstruct));
  s.create_=reinterpret_cast<abi::arm64::context::Create>(finder(abi::arm64::context::kCreate));
  s.walk17_=finder(abi::arm64::stack::kWalk17)!=nullptr;
  s.walk_old_=finder(abi::arm64::stack::kWalk)!=nullptr;
  const Family families[]={Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid17};
  for(size_t i=0;i<s.services_.size();++i)
   s.bound_[i]=active_frame_recovery_proof::Service::Bind(families[i],finder,&s.services_[i]);
  if(!s.construct_||!s.create_||(!s.walk17_&&!s.walk_old_))return false;
  *out=s;return true;
 }
 bool Prepare(Prepared* p)const {
  if(!p||p->context||!create_)return false;p->context.reset(create_());return p->context!=nullptr;
 }
 // Caller owns a real suspended target/exclusive mutator scope, strong method
 // and witness roots, and a frozen mapping reader. No JNI/OS mapping discovery,
 // native allocation, hooks, suspension, or code mutation occurs here.
 // Public oracles must independently cover native, static, instance methods.
 bool Calibrate(void* target,void* jit,const MethodOracle* methods,size_t count,
    const active_frame_recovery_proof::PhysicalOracle& physical,
    decoded_frame_recovery::Location* scratch,size_t capacity,
    active_frame_recovery_proof::Service::Readable readable,Prepared& prepared,
    Result* output)const {
  if(!output||!target||!jit||!readable||!prepared.context||!scratch||!methods||count<3)return false;
  // Native decoder container has sixteen inline slots; this independent
  // calibration fixture is deliberately bounded to avoid any stopped allocation.
  for(const auto& caller:physical.callers)if(caller.registers>16)return false;
  bool model11=Methods(method_metadata::kAndroid11,methods,count);
  bool model12=Methods(method_metadata::kAndroid12Onward,methods,count);
  if(model11==model12)return false;
  // Shared source constructor has the same signature in all declarations.
  // Supply the largest COMPLETE C++ callback object, never invoke WalkStack
  // until its independent thread/context/kind echo matches a fixed geometry.
  Echo echo{};void* own_vptr=nullptr;std::memcpy(&own_vptr,static_cast<void*>(&echo),sizeof(own_vptr));
  construct_(&echo,target,prepared.context.get(),abi::arm64::stack::WalkKind::kIncludeInlined,true);
  std::memcpy(static_cast<void*>(&echo),&own_vptr,sizeof(own_vptr));
  if(echo.thread!=target||echo.kind!=abi::arm64::stack::WalkKind::kIncludeInlined)return false;
  const auto mask=abi::arm64::stack::ConstructorContextEchoMask(&echo,sizeof(echo),prepared.context.get());
  Result result;size_t index;
  if(mask==9&&model11&&walk_old_&&!walk17_){result.geometry=Geometry::kLegacy368;result.scanner_family=Family::kAndroid11;index=0;}
  else if(mask==2&&model12&&walk_old_&&!walk17_){result.geometry=Geometry::kLegacy376;result.scanner_family=Family::kAndroid12;index=1;}
  else if(mask==4&&model12&&walk_old_&&!walk17_){result.geometry=Geometry::kCompact344;result.scanner_family=Family::kAndroid13;index=bound_[3]?3:2;}
  else if(mask==9&&model12&&walk17_&&!walk_old_){result.geometry=Geometry::kModern368;result.scanner_family=Family::kAndroid17;index=4;}
  else return false;
  if(!bound_[index])return false;
  const active_frame_recovery_proof::Oracle oracle{physical.callers[0],{physical.callers[1],physical.callers[2]},active_frame_recovery_proof::Shape::kThreePhysical};
  result.proof=services_[index].ProvePrepared(target,jit,oracle,scratch,capacity,prepared.context.get(),readable);
  const auto& p=result.proof;
  if(p.failure!=active_frame_recovery_proof::Failure::kNone||p.frames!=3||p.physical_matches!=3||p.physical_owner_mask!=7||p.inline_matches||p.header_checks!=3||p.dex_pc_checks!=3||p.integers!=3||p.references!=3||p.location_reads!=6)return false;
  result.failure="";*output=result;return true;
 }
 private:
 struct Echo final:abi::arm64::stack::Visitor<abi::arm64::android12::CodeInfoPair> {
  bool VisitFrame()override{return false;}
 };
 static bool Methods(const method_metadata::Layout& layout,const MethodOracle* m,size_t count){
  bool native=false,stat=false,instance=false;
  constexpr uint32_t public_mask=0xffffu;
  for(size_t i=0;i<count;++i){
   if(!m[i].method)return false;
   for(size_t j=0;j<i;++j)if(m[i].method==m[j].method||m[i].dex_index==m[j].dex_index)return false;
   method_metadata::Identity identity;
   if(!method_metadata::ReadIdentity(layout,m[i].method,m[i].extent,&identity)||identity.dex_method_index!=m[i].dex_index||(identity.access_flags&public_mask)!=(m[i].java_modifiers&public_mask))return false;
   if(m[i].java_modifiers&0x100u)native=true;
   else if(m[i].java_modifiers&8u)stat=true;else instance=true;
  }
  return native&&stat&&instance;
 }
 abi::arm64::stack::Construct construct_=nullptr;
 abi::arm64::context::Create create_=nullptr;
 bool walk17_=false,walk_old_=false;
 std::array<active_frame_recovery_proof::Service,5> services_{};
 std::array<bool,5> bound_{};
};
}

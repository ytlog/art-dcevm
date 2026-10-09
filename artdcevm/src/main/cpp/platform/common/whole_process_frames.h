#pragma once
#include "platform/common/native_source_stop.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/common/decoded_frame_recovery.h"
#include "platform/abi/arm64/dex_register_map.h"
#include "platform/abi/arm64/context.h"
#include "platform/abi/arm64/android14/frame_registers.h"
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android12/art_method.h"
#include <vector>
#include <cstring>
namespace artdcevm::whole_process_frames {
using Family=code_info_view::SourceFamily;
static_assert(offsetof(abi::arm64::android11::Method11,access_flags)==offsetof(abi::arm64::android12::Method12,access_flags));
struct Report {
 size_t registered_threads=0,walked_threads=0,empty_threads=0,frames=0,transitions=0,
   shadow_frames=0,native_frames=0,compiled_frames=0,inlined_frames=0,map_locations=0,native_value_reads=0;
 const char* failure="not-scanned";
 bool accepted=false;
};
// This is a readonly eligibility scanner, not a deoptimization operation.
// Bind's fixed family must already have independent native geometry and
// public/native ArtMethod access-flag prefix calibration.
// Every actual compiled callback still proves its own named deopt flag, JIT
// ownership, full native map and exact original current DexPc. AOT/Nterp,
// missing flags/maps/unknown layouts are never inferred safe from debug fixtures.
class Service {
 public:
  template<class Finder> static bool Bind(Family family,Finder finder,Service* out) {
    if(!out||(family!=Family::kAndroid11&&family!=Family::kAndroid12&&family!=Family::kAndroid13&&family!=Family::kAndroid14&&family!=Family::kAndroid15&&family!=Family::kAndroid16&&family!=Family::kAndroid17))return false;Service c;c.family_=family;
    const auto declaration=family==Family::kAndroid11?stack_visitor_service::Declaration::kAndroid11:
      family==Family::kAndroid12?stack_visitor_service::Declaration::kAndroid12:
      family==Family::kAndroid17?stack_visitor_service::Declaration::kAndroid17:stack_visitor_service::Declaration::kAndroid13To16;
    if(!stack_visitor_service::Service::Bind(declaration,finder,&c.stack_))return false;
    c.create_=reinterpret_cast<abi::arm64::context::Create>(finder(abi::arm64::context::kCreate));
    c.construct_=finder(abi::arm64::android13::kConstructCodeInfo);
    c.map_=finder(family==Family::kAndroid11?abi::arm64::android11::kFindStackMap:abi::arm64::android13::kFindStackMap);
    c.decode_=reinterpret_cast<abi::arm64::dex_register_map::Decode>(finder(abi::arm64::dex_register_map::kDecode));
    c.dex_pc_=reinterpret_cast<uint32_t(*)(const void*,bool)>(finder("_ZNK3art12StackVisitor8GetDexPcEb"));
    c.offset_=reinterpret_cast<abi::arm64::android14::frame_registers::NativePcOffset>(finder(abi::arm64::android14::frame_registers::kNativePcOffset));
    c.jit_=reinterpret_cast<bool(*)(const void*,const void*)>(finder("_ZNK3art3jit12JitCodeCache10ContainsPcEPKv"));
    c.nterp_=reinterpret_cast<bool(*)(const void*)>(finder("_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv"));
    c.flag_=reinterpret_cast<bool(*)(const void*)>(finder("_ZNK3art20OatQuickMethodHeader23HasShouldDeoptimizeFlagEv"));
    c.fast_=finder(abi::arm64::android12::frame_registers::kFast);
    c.canonical_=reinterpret_cast<abi::arm64::android14::frame_registers::Canonical>(finder(abi::arm64::android14::frame_registers::kCanonical));
    c.debugger_=reinterpret_cast<abi::arm64::android14::frame_registers::DebuggerValue>(finder(abi::arm64::android14::frame_registers::kDebuggerValue));
    c.accessible_=reinterpret_cast<abi::arm64::android14::frame_registers::AccessibleRegister>(finder(abi::arm64::android14::frame_registers::kAccessibleRegister));
    if(!c.create_||!c.construct_||!c.map_||!c.decode_||!c.dex_pc_)return false;
    *out=c;return true;
  }
  // Allocate all Context/container/scratch storage BEFORE Session::Begin.
  // Native CodeItem register counts are uint16; no walk-time native allocations.
  struct Prepared {
    Prepared():locations(65535),native_map(65535){}
    std::vector<decoded_frame_recovery::Location> locations;
    abi::arm64::dex_register_map::Map native_map;
    abi::arm64::context::Prefix* context=nullptr;
    ~Prepared(){delete context;}
    Prepared(const Prepared&)=delete;Prepared& operator=(const Prepared&)=delete;
  };
  bool Prepare(Prepared* storage)const {
    if(!storage||storage->context||!create_)return false;
    storage->context=create_();return storage->context!=nullptr;
  }
  Report Scan(const native_source_stop::Session& owner,void* self,
              const native_cache::Service& threads,void* jit,Prepared& storage)const {
    Report report;if(!owner.IsStopped(self)||!threads.PreparedForCurrentThread()||!storage.context){report.failure="whole-scan-lacks-actual-source-stop-or-preparation";return report;}
    Request request{*this,owner,self,jit,storage,report};
    const bool ok=threads.VisitStoppedWorld([](void* thread,void* raw){auto& r=*static_cast<Request*>(raw);return r.service.Thread(r,thread);},&request,&report.registered_threads,nullptr);
    if(ok&&report.registered_threads>0&&report.registered_threads==report.walked_threads&&request.owner_seen&&owner.IsStopped(self)){report.accepted=true;report.failure="";}
    else if(!std::strcmp(report.failure,"not-scanned"))report.failure="whole-scan-native-enumeration-or-owner-refused";
    return report;
  }
 private:
  struct Request {const Service& service;const native_source_stop::Session& owner;void* self;void* jit;Prepared& storage;Report& report;bool owner_seen=false;};
  template<class Info,class Pair> struct Visitor final:abi::arm64::stack::Visitor<Pair> {
    unsigned char native_tail[sizeof(abi::arm64::stack::Visitor12)-sizeof(abi::arm64::stack::Visitor<Pair>)+8]{};
    Request& request;void* target;size_t before;
    Visitor(Request& r,void* t):abi::arm64::stack::Visitor<Pair>{},request(r),target(t),before(r.report.frames){}
    bool Reject(const char* reason){request.report.failure=reason;return false;}
    bool VisitFrame()override {
      auto& r=request;const auto& s=r.service;
      if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context)
        return Reject("whole-frame-owner-native-thread-context-changed");
      void* method=nullptr;if(!s.stack_.Method(this,sizeof(*this),&method))return Reject("whole-frame-native-method-unavailable");
      if(!method){++r.report.transitions;return true;}
      ++r.report.frames;
      if(this->shadow_frame&&!this->quick_frame){++r.report.shadow_frames;return true;}
      if(!this->quick_frame||this->shadow_frame)return Reject("whole-frame-unknown-native-cursor");
      // Original GetMethod owns the live ArtMethod. The fixed 11--17 common
      // prefix is Class32, access_flags32; only JNI kAccNative is used here.
      const bool physical=this->inline_frames.begin.row==this->inline_frames.end.row;
      if(physical&&*this->quick_frame!=method)return Reject("whole-frame-original-method-versus-current-quick-frame-mismatch");
      uint32_t flags;std::memcpy(&flags,static_cast<const unsigned char*>(method)+offsetof(abi::arm64::android12::Method12,access_flags),4);
      if(flags&0x100u){if(!physical)return Reject("whole-frame-unproved-native-inlinee");++r.report.native_frames;return true;}
      if(!this->header||!s.flag_||!s.flag_(this->header))return Reject("whole-frame-unmarked-or-unknown-compiled-code");
      if(s.family_==Family::kAndroid11||(!s.fast_&&(!s.canonical_||!s.debugger_)))return Reject("whole-frame-original-location-reader-unavailable");
      if(!r.jit||!s.jit_||!s.jit_(r.jit,reinterpret_cast<void*>(this->pc)))return Reject("whole-frame-AOT-or-unowned-compiled-code");
      if(!s.nterp_||s.nterp_(this->header))return Reject("whole-frame-Nterp-or-unproved-header-kind");
      const uintptr_t entry=reinterpret_cast<uintptr_t>(this->header)+(s.family_==Family::kAndroid11?8u:4u);
      if(this->pc<entry)return Reject("whole-frame-PC-before-source-code");
      const uintptr_t offset=s.offset_?s.offset_(this):this->pc-entry;
      if(offset!=this->pc-entry)return Reject("whole-frame-original-PC-offset-mismatch");
      const auto* original_header=this->header;
      auto* original_quick=this->quick_frame;const auto original_pc=this->pc;
      const auto original_begin=this->inline_frames.begin.row,original_end=this->inline_frames.end.row;
      alignas(Info) unsigned char info_storage[sizeof(abi::arm64::android12::CodeInfo)]{};
      auto* info=reinterpret_cast<Info*>(info_storage);
      reinterpret_cast<void(*)(Info*,const void*)>(s.construct_)(info,this->header);
      abi::arm64::android11::TableIterator map;
      if(s.family_==Family::kAndroid11){if(offset>UINT32_MAX)return Reject("whole-frame-PC-offset-overflow");map=reinterpret_cast<abi::arm64::android11::FindStackMap>(s.map_)(reinterpret_cast<abi::arm64::android11::CodeInfo*>(info),offset,2);}
      else map=reinterpret_cast<abi::arm64::android11::TableIterator(*)(const Info*,uintptr_t,int32_t)>(s.map_)(info,offset,2);
      code_info_view::View view;decoded_frame_recovery::RegisterRange range;
      if(map.table!=&info->stack_maps||!code_info_view::ReadSourceObject(s.family_,info,sizeof(info_storage),&view))return Reject("whole-frame-original-map-source-geometry-mismatch");
      if(view.code_size&&(!*view.code_size||offset>*view.code_size))return Reject("whole-frame-PC-outside-native-code-size");
      uint32_t pc;
      if(!(physical?view.StackMaps().Read(map.row,2,&pc):view.InlineInfos().Read(this->inline_frames.end.row-1,1,&pc))||pc==UINT32_MAX||s.dex_pc_(this,false)!=pc)return Reject("whole-frame-original-versus-complete-map-DexPc-mismatch");
      if(!decoded_frame_recovery::Decode(view,map.row,this->inline_frames.begin.row,this->inline_frames.end.row,r.storage.locations.data(),r.storage.locations.size(),&range))return Reject("whole-frame-incomplete-dex-register-map");
      auto& native=r.storage.native_map;native.count=range.count;
      s.decode_(info,map.row,range.first,&native);
      if(native.count!=range.count)return Reject("whole-frame-native-map-count-changed");
      for(uint32_t index=0;index<range.count;++index){auto own=r.storage.locations[index];auto actual=native.data()[index];
        if(actual.kind!=own.kind||(actual.kind>=0&&static_cast<uint32_t>(actual.value)!=own.value))return Reject("whole-frame-original-versus-bounded-map-location-mismatch");
        if(actual.kind==0&&(info->frame_size>UINT32_MAX/16||info->frame_size*16<4||own.value>info->frame_size*16-4))return Reject("whole-frame-stack-location-outside-source-frame");
        if(actual.kind>=2&&actual.kind<=5){if(own.value>=32)return Reject("whole-frame-register-location-outside-native-context");
          const bool core=actual.kind==2||actual.kind==3;
          uint32_t original_register;
          if(!s.accessible_||!s.accessible_(this,own.value,actual.kind,&original_register))return Reject("whole-frame-original-register-context-unavailable");
          if(core?!r.storage.context->IsAccessibleGPR(own.value):!r.storage.context->IsAccessibleFPR(own.value))return Reject("whole-frame-native-register-context-unavailable");}
        if(actual.kind>=0){
          uint32_t original=0,independent=0;
          const abi::arm64::android12::frame_registers::Location location{actual.kind,actual.value};
          if(s.fast_){if(!reinterpret_cast<abi::arm64::android12::frame_registers::Fast>(s.fast_)(this,location,&original))return Reject("whole-frame-original-fast-location-read-refused");}
          else {uint32_t overridden;
            if(s.debugger_(this,static_cast<uint16_t>(index),abi::arm64::android14::frame_registers::Kind::kInt,&overridden))return Reject("whole-frame-debugger-shadow-override");
            if(!s.canonical_(this,method,static_cast<uint16_t>(index),abi::arm64::android14::frame_registers::Kind::kInt,&original,abi::arm64::android14::frame_registers::OptionalLocation{location},false))return Reject("whole-frame-original-canonical-location-read-refused");}
          if(actual.kind==0)std::memcpy(&independent,reinterpret_cast<const unsigned char*>(this->quick_frame)+own.value,4);
          else if(actual.kind==1)independent=own.value;
          else {uintptr_t bits=(actual.kind==2||actual.kind==3)?r.storage.context->GetGPR(own.value):r.storage.context->GetFPR(own.value);if(actual.kind==3||actual.kind==5)bits>>=32;independent=static_cast<uint32_t>(bits);}
          if(original!=independent)return Reject("whole-frame-original-versus-physical-value-mismatch");
          ++r.report.native_value_reads;
        }
      }
      void* current=nullptr;
      if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context||
         this->header!=original_header||this->quick_frame!=original_quick||this->pc!=original_pc||
         this->shadow_frame||this->inline_frames.begin.row!=original_begin||this->inline_frames.end.row!=original_end||
         !s.stack_.Method(this,sizeof(*this),&current)||current!=method||s.dex_pc_(this,false)!=pc)return Reject("whole-frame-source-owner-or-native-cursor-lost-after-map");
      ++r.report.compiled_frames;if(!physical)++r.report.inlined_frames;r.report.map_locations+=range.count;return true;
    }
  };
  template<class Info,class Pair> bool Walk(Request& r,void* target)const {
    r.storage.context->Reset();Visitor<Info,Pair> visitor(r,target);
    if(!stack_.Construct(&visitor,sizeof(visitor),target,r.storage.context,abi::arm64::stack::WalkKind::kIncludeInlined)||
       visitor.thread!=target||visitor.context!=r.storage.context||visitor.kind!=abi::arm64::stack::WalkKind::kIncludeInlined){r.report.failure="whole-thread-original-constructor-geometry-mismatch";return false;}
    if(!stack_.Walk(&visitor,sizeof(visitor),true)){r.report.failure="whole-thread-native-walk-unavailable";return false;}
    if(std::strcmp(r.report.failure,"not-scanned"))return false;
    ++r.report.walked_threads;if(r.report.frames==visitor.before)++r.report.empty_threads;return true;
  }
  bool Thread(Request& r,void* target)const {
    if(!r.owner.IsStopped(r.self)||!target){r.report.failure="whole-thread-source-stop-owner-lost";return false;}
    r.owner_seen|=target==r.self;
    if(family_==Family::kAndroid11)return Walk<abi::arm64::android11::CodeInfo,abi::arm64::android11::CodeInfoPair>(r,target);
    if(family_==Family::kAndroid12)return Walk<abi::arm64::android12::CodeInfo,abi::arm64::android12::CodeInfoPair>(r,target);
    if(family_==Family::kAndroid17)return Walk<abi::arm64::android17::CodeInfo,abi::arm64::android17::CodeInfoPair>(r,target);
    return Walk<abi::arm64::android13::CodeInfo,abi::arm64::android13::CodeInfoPair>(r,target);
  }
  Family family_=Family::kAndroid13;stack_visitor_service::Service stack_;
  abi::arm64::context::Create create_=nullptr;void* construct_=nullptr;void* map_=nullptr;
  abi::arm64::dex_register_map::Decode decode_=nullptr;
  uint32_t(*dex_pc_)(const void*,bool)=nullptr;
  abi::arm64::android14::frame_registers::NativePcOffset offset_=nullptr;
  void* fast_=nullptr;
  abi::arm64::android14::frame_registers::Canonical canonical_=nullptr;
  abi::arm64::android14::frame_registers::DebuggerValue debugger_=nullptr;
  abi::arm64::android14::frame_registers::AccessibleRegister accessible_=nullptr;
  bool(*jit_)(const void*,const void*)=nullptr;bool(*nterp_)(const void*)=nullptr;bool(*flag_)(const void*)=nullptr;
};
}

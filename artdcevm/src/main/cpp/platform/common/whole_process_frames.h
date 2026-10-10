#include "platform/common/native_boot_method_catalog.h"
#pragma once
#include "platform/common/native_boot_aot_unaffected.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/common/decoded_frame_recovery.h"
#include "platform/common/native_frame_async_recovery.h"
#include "platform/common/native_frame_code_owner.h"
#include "platform/common/native_runtime_frame.h"
#include "platform/common/source_commit.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/abi/arm64/dex_register_map.h"
#include "platform/abi/arm64/context.h"
#include "platform/abi/arm64/android14/frame_registers.h"
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android12/art_method.h"
#include "platform/abi/arm64/android11/quick_method_header.h"
#include "platform/abi/arm64/android12/quick_method_header.h"
#include <vector>
#include <algorithm>
#include <cstring>
namespace artdcevm::whole_process_frames {
namespace detail {bool VisitThread(void*,void*);}
class Service;
// Borrowed capability: Service, Prepared, Plan, Session, thread service and
// ReadContext must outlive it. The owner clears it before releasing those
// resources. Validation rewalks the actual window; it cannot extend a lifetime.
class StoppedRecoveryProof final {
 public:
  bool Validate(const source_commit::Plan&)const noexcept;
  bool ValidateFinal(const source_commit::Plan&plan)const noexcept {return final_&&Validate(plan);}
 private:
  friend class Service;
  const Service* service_=nullptr;const source_commit::Plan* plan_=nullptr;
  const native_source_stop::Session* stop_=nullptr;const native_cache::Service* threads_=nullptr;
  const modern_field_metadata::ReadContext* context_=nullptr;
  void* self_=nullptr;void* jit_=nullptr;void* storage_=nullptr;
  uint64_t generation_=0;bool final_=false,collected_=false;
};
using Family=code_info_view::SourceFamily;
static_assert(offsetof(abi::arm64::android11::Method11,access_flags)==offsetof(abi::arm64::android12::Method12,access_flags));
struct Report {
 uintptr_t failure_thread=0,failure_method=0,failure_quick=0,failure_pc=0,failure_header=0;
 uint32_t failure_declaring_class=0,failure_flags=0,failure_dex_index=0;
 bool failure_method_prefix=false,failure_physical=false;
 const char* aot_declaring_class=nullptr;const char* aot_method=nullptr;const char* aot_descriptor=nullptr;
 size_t registered_threads=0,walked_threads=0,empty_threads=0,frames=0,transitions=0,
   async_frames=0,aot_frames=0,unaffected_aot_frames=0,shadow_frames=0,native_frames=0,runtime_frames=0,compiled_frames=0,inlined_frames=0,map_locations=0,native_value_reads=0;
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
    return BindDeclaration(family,finder,out,true);
  }
  // Requires independently calibrated constructor geometry and public method
  // prefix only. All shadow/native frames can be scanned; the first compiled
  // callback fails closed before any header/map/reader interpretation.
  template<class Finder> static bool BindGeometryOnly(Family family,Finder finder,Service* out) {
    return BindDeclaration(family,finder,out,false);
  }
 private:
  template<class Finder> static bool BindDeclaration(Family family,Finder finder,Service* out,bool compiled_ready) {
    if(!out||(family!=Family::kAndroid11&&family!=Family::kAndroid12&&family!=Family::kAndroid13&&family!=Family::kAndroid14&&family!=Family::kAndroid15&&family!=Family::kAndroid16&&family!=Family::kAndroid17))return false;Service c;c.family_=family;c.compiled_ready_=compiled_ready;
    const auto declaration=family==Family::kAndroid11?stack_visitor_service::Declaration::kAndroid11:
      family==Family::kAndroid12?stack_visitor_service::Declaration::kAndroid12:
      family==Family::kAndroid17?stack_visitor_service::Declaration::kAndroid17:stack_visitor_service::Declaration::kAndroid13To16;
    if(!stack_visitor_service::Service::Bind(declaration,finder,&c.stack_))return false;
    c.async_=native_frame_async_recovery::Service::Bind(finder);
    c.code_owner_=native_frame_code_owner::Service::Bind(finder);
    c.runtime_=native_runtime_frame::Service::Bind(finder);
    c.create_=reinterpret_cast<abi::arm64::context::Create>(finder(abi::arm64::context::kCreate));
    c.construct_=finder(abi::arm64::android13::kConstructCodeInfo);
    c.map_=finder(family==Family::kAndroid11?abi::arm64::android11::kFindStackMap:abi::arm64::android13::kFindStackMap);
    c.decode_=reinterpret_cast<abi::arm64::dex_register_map::Decode>(finder(abi::arm64::dex_register_map::kDecode));
    c.dex_pc_=reinterpret_cast<uint32_t(*)(const void*,bool)>(finder("_ZNK3art12StackVisitor8GetDexPcEb"));
    c.offset_=reinterpret_cast<abi::arm64::android14::frame_registers::NativePcOffset>(finder(abi::arm64::android14::frame_registers::kNativePcOffset));
    c.jit_=reinterpret_cast<bool(*)(const void*,const void*)>(finder("_ZNK3art3jit12JitCodeCache10ContainsPcEPKv"));
    c.nterp_=reinterpret_cast<bool(*)(const void*)>(finder("_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv"));
    c.flag_=reinterpret_cast<bool(*)(const void*)>(finder("_ZNK3art20OatQuickMethodHeader23HasShouldDeoptimizeFlagEv"));
    if(family==Family::kAndroid11){
      c.legacy_fast_=reinterpret_cast<abi::arm64::android11::frame_registers::Fast>(finder(abi::arm64::android11::frame_registers::kFast));
      c.legacy_canonical_=reinterpret_cast<abi::arm64::android11::frame_registers::Canonical>(finder(abi::arm64::android11::frame_registers::kCanonical));
      c.legacy_accessible_=reinterpret_cast<abi::arm64::android11::frame_registers::AccessibleRegister>(finder(abi::arm64::android11::frame_registers::kAccessibleRegister));
    }else {
      c.fast_=finder(abi::arm64::android12::frame_registers::kFast);
      if(family==Family::kAndroid12)c.legacy_canonical_=reinterpret_cast<abi::arm64::android12::frame_registers::Canonical>(finder(abi::arm64::android12::frame_registers::kCanonical));
    }
    c.canonical_=reinterpret_cast<abi::arm64::android14::frame_registers::Canonical>(finder(abi::arm64::android14::frame_registers::kCanonical));
    c.debugger_=reinterpret_cast<abi::arm64::android14::frame_registers::DebuggerValue>(finder(abi::arm64::android14::frame_registers::kDebuggerValue));
    c.accessible_=reinterpret_cast<abi::arm64::android14::frame_registers::AccessibleRegister>(finder(abi::arm64::android14::frame_registers::kAccessibleRegister));
    if(!c.create_||(compiled_ready&&(!c.construct_||!c.map_||!c.decode_||!c.dex_pc_)))return false;
    *out=c;return true;
  }
 public:
  // Allocate all Context/container/scratch storage BEFORE Session::Begin.
  // Native CodeItem register counts are uint16; no walk-time native allocations.
  struct Prepared {
    Prepared():locations(65535),native_map(65535){}
    std::vector<decoded_frame_recovery::Location> locations;
    abi::arm64::dex_register_map::Map native_map;
    abi::arm64::context::Prefix* context=nullptr;
    source_commit::Plan recovery_identity;
    const source_commit::Plan* recovery_plan=nullptr;
    uint64_t recovery_generation=0;
    const native_boot_method_catalog::Rooted* boot_catalog=nullptr;
    const source_handle_roots::StoppedReader* boot_reader=nullptr;
    const native_boot_aot_unaffected::Proof* boot_unaffected=nullptr;
    ~Prepared(){delete context;}
    Prepared(const Prepared&)=delete;Prepared& operator=(const Prepared&)=delete;
  };
  bool Prepare(Prepared* storage)const {
    if(!storage||storage->context||!create_)return false;
    storage->context=create_();return storage->context!=nullptr;
  }
  // Ordinary preparation only. Preallocate the complete exact Plan identity;
  // stopped Capture refreshes values in-place without growing native storage.
  bool PrepareRecoveryPlan(const source_commit::Plan&plan,Prepared*storage)const {
    if(!storage||storage->recovery_plan)return false;
    storage->recovery_identity=plan;storage->recovery_plan=&plan;return true;
  }
  Report ScanRecovery(const native_source_stop::Session&owner,void*self,
      const native_cache::Service&threads,void*jit,Prepared&storage,
      const source_commit::Plan&plan,const modern_field_metadata::ReadContext&context,
      StoppedRecoveryProof*proof=nullptr)const {
    if(proof)*proof={};Report denied;
    if(storage.recovery_plan!=&plan||!context.owner||!context.extent||!SameShape(storage.recovery_identity,plan)){
      denied.failure="recovery-lacks-prepared-exact-Plan-or-owned-read-context";return denied;
    }
    auto report=ScanImpl(owner,self,threads,jit,storage,&context);
    if(!report.accepted||!proof)return report;
    if(storage.recovery_generation==UINT64_MAX){report.accepted=false;report.failure="recovery-generation-exhausted";return report;}
    RefreshIdentity(plan,storage.recovery_identity);++storage.recovery_generation;
    proof->service_=this;proof->plan_=&plan;proof->stop_=&owner;proof->threads_=&threads;proof->context_=&context;
    proof->self_=self;proof->jit_=jit;proof->storage_=&storage;proof->generation_=storage.recovery_generation;
    proof->final_=owner.IsFinalWindow(self);proof->collected_=owner.IsCollectedStop(self);return report;
  }
  Report Scan(const native_source_stop::Session& owner,void* self,
              const native_cache::Service& threads,void* jit,Prepared& storage)const {
    return ScanImpl(owner,self,threads,jit,storage,nullptr);
  }
 private:
  friend class StoppedRecoveryProof;
  static bool SameShape(const source_commit::Plan&a,const source_commit::Plan&b)noexcept {
    return a.old_classes.size()==b.old_classes.size()&&a.new_classes.size()==b.new_classes.size()&&a.old_instances.size()==b.old_instances.size()&&a.new_instances.size()==b.new_instances.size()&&a.class_root_origins.size()==b.class_root_origins.size();
  }
  static void RefreshIdentity(const source_commit::Plan&from,source_commit::Plan&to)noexcept {
    to.mirror=from.mirror;to.new_classes_root=from.new_classes_root;to.abstract_instances=from.abstract_instances;
    std::copy(from.old_classes.begin(),from.old_classes.end(),to.old_classes.begin());std::copy(from.new_classes.begin(),from.new_classes.end(),to.new_classes.begin());
    std::copy(from.old_instances.begin(),from.old_instances.end(),to.old_instances.begin());std::copy(from.new_instances.begin(),from.new_instances.end(),to.new_instances.begin());
    std::copy(from.class_root_origins.begin(),from.class_root_origins.end(),to.class_root_origins.begin());
  }
  Report ScanImpl(const native_source_stop::Session&owner,void*self,const native_cache::Service&threads,void*jit,Prepared&storage,const modern_field_metadata::ReadContext*context)const {
    Report report;if(!owner.IsStopped(self)||!threads.PreparedForCurrentThread()||!storage.context){report.failure="whole-scan-lacks-actual-source-stop-or-preparation";return report;}
    Request request{*this,owner,self,jit,storage,report,context};
    const bool ok=threads.VisitStoppedWorld(detail::VisitThread,&request,&report.registered_threads,nullptr);
    if(ok&&report.registered_threads>0&&report.registered_threads==report.walked_threads&&request.owner_seen&&owner.IsStopped(self)){report.accepted=true;report.failure="";}
    else if(!std::strcmp(report.failure,"not-scanned"))report.failure="whole-scan-native-enumeration-or-owner-refused";
    return report;
  }
 private:
  bool MarkedHeader(const void* header)const noexcept {
    if(flag_)return flag_(header);
    // Fixed legacy inline method; a missing symbol alone grants nothing.
    // The actual native visitor supplies its live header, in the owned stop.
    uint32_t data=0;
    if(family_==Family::kAndroid11){
      std::memcpy(&data,static_cast<const unsigned char*>(header)+offsetof(abi::arm64::android11::QuickMethodHeader,code_size),sizeof(data));
      return (data&abi::arm64::android11::kShouldDeoptimizeMask)!=0;
    }
    if(family_==Family::kAndroid12){
      std::memcpy(&data,header,sizeof(data));
      return (data&abi::arm64::android12::kShouldDeoptimizeMask)!=0;
    }
    return false;
  }
  struct Request {const Service& service;const native_source_stop::Session& owner;void* self;void* jit;Prepared& storage;Report& report;const modern_field_metadata::ReadContext* recovery;bool owner_seen=false;};
  friend bool detail::VisitThread(void*,void*);
  template<class Info,class Pair> struct Visitor final:abi::arm64::stack::Visitor<Pair> {
    unsigned char native_tail[sizeof(abi::arm64::stack::Visitor12)-sizeof(abi::arm64::stack::Visitor<Pair>)+8]{};
    Request& request;void* target;size_t before;
    void** next_quick=nullptr;uintptr_t next_pc=0;bool runtime_successor=false;
    void* observed_method=nullptr;
    Visitor(Request& r,void* t):abi::arm64::stack::Visitor<Pair>{},request(r),target(t),before(r.report.frames){}
    bool Reject(const char* reason){
     auto& report=request.report;report.failure=reason;report.failure_thread=uintptr_t(target);report.failure_method=uintptr_t(observed_method);
     report.failure_quick=uintptr_t(this->quick_frame);report.failure_pc=this->pc;report.failure_header=uintptr_t(this->header);
     report.failure_physical=this->inline_frames.begin.row==this->inline_frames.end.row;report.failure_method_prefix=false;
     report.failure_declaring_class=report.failure_flags=report.failure_dex_index=0;
     const size_t dex_offset=request.service.family_==Family::kAndroid11?offsetof(abi::arm64::android11::Method11,dex_method_index):offsetof(abi::arm64::android12::Method12,dex_method_index);size_t extent=0;
     if(observed_method&&request.owner.IsStopped(request.self)&&this->thread==target&&this->context==request.storage.context&&native_runtime_frame::Extent(request.recovery,observed_method,&extent)&&extent>=dex_offset+4){
      std::memcpy(&report.failure_declaring_class,observed_method,4);
      report.failure_flags=method_metadata::ReadAccessFlags(observed_method);
      std::memcpy(&report.failure_dex_index,static_cast<unsigned char*>(observed_method)+dex_offset,4);report.failure_method_prefix=true;
     }
     return false;
    }
    bool VisitFrame()override {
      auto& r=request;const auto& s=r.service;
      observed_method=nullptr;
      if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context)
        return Reject("whole-frame-owner-native-thread-context-changed");
      if(runtime_successor){
       if(this->quick_frame!=next_quick||this->pc!=next_pc)return Reject("whole-runtime-original-walker-successor-differs");
       runtime_successor=false;
      }
      void* method=nullptr;if(!s.stack_.Method(this,sizeof(*this),&method))return Reject("whole-frame-native-method-unavailable");
      observed_method=method;
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
      if(!s.compiled_ready_)return Reject("whole-frame-compiled-geometry-not-calibrated");
      if(!this->header){
       auto& report=r.report;report.failure_thread=uintptr_t(target);report.failure_method=uintptr_t(method);
       report.failure_quick=uintptr_t(this->quick_frame);report.failure_pc=this->pc;report.failure_header=uintptr_t(this->header);
       report.failure_physical=physical;size_t extent=0;
       const size_t dex_offset=s.family_==Family::kAndroid11?offsetof(abi::arm64::android11::Method11,dex_method_index):offsetof(abi::arm64::android12::Method12,dex_method_index);
       if(native_runtime_frame::Extent(r.recovery,method,&extent)&&extent>=dex_offset+4){
        std::memcpy(&report.failure_declaring_class,method,4);report.failure_flags=flags;
        std::memcpy(&report.failure_dex_index,static_cast<unsigned char*>(method)+dex_offset,4);report.failure_method_prefix=true;
       }
       if(!report.failure_method_prefix||report.failure_dex_index!=UINT32_MAX)return Reject("whole-frame-null-header-managed-method");
       if(!physical)return Reject("whole-runtime-inline-frame-unproved");
       if(!s.runtime_.Bound())return Reject("whole-runtime-frame-info-reader-unavailable");
       native_runtime_frame::FrameInfo info;const auto original_pc=this->pc;auto*original_quick=this->quick_frame;
       if(!s.runtime_.Observe(this,method,this->quick_frame,s.family_==Family::kAndroid11,r.recovery,&info))return Reject("whole-runtime-frame-geometry-or-method-unproved");
       void* again=nullptr;if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context||this->header||this->quick_frame!=original_quick||this->pc!=original_pc||!s.stack_.Method(this,sizeof(*this),&again)||again!=method)return Reject("whole-runtime-original-cursor-changed");
       const auto base=uintptr_t(original_quick);if(info.size>UINTPTR_MAX-base)return Reject("whole-runtime-frame-address-overflow");
       // WalkStack(include_transitions=true) advances with this exact LR and
       // SP. Even its last quick frame is followed by the fragment transition
       // callback; no guessed PC normalization or missing-successor exemption.
       next_quick=reinterpret_cast<void**>(base+info.size);std::memcpy(&next_pc,reinterpret_cast<void*>(base+info.size-8),8);
       runtime_successor=true;++report.runtime_frames;return true;
      }
      if(!r.recovery&&!s.MarkedHeader(this->header))return Reject("whole-frame-compiled-header-unmarked");
      bool unaffected_aot=false;
      if(r.recovery){
        if(!s.async_.Bound())return Reject("whole-frame-original-async-recovery-reader-unavailable");
        const auto kind=s.code_owner_.Observe(*this->quick_frame,this->pc,this->header,r.jit);
        if(kind==native_frame_code_owner::Kind::Aot)++r.report.aot_frames;
        if(kind==native_frame_code_owner::Kind::Unknown)return Reject("whole-frame-recovery-code-owner-unproved");
        const bool asynchronous=s.async_.Query(*this->quick_frame,this->pc,*r.recovery);
        if(!asynchronous){
         if(kind==native_frame_code_owner::Kind::Aot&&r.storage.boot_catalog&&r.storage.boot_reader){
          const auto*body=r.storage.boot_catalog->Lookup(*this->quick_frame,*r.storage.boot_reader,*r.recovery);
          if(body){r.report.aot_declaring_class=body->declaring_signature.c_str();r.report.aot_method=body->name.c_str();r.report.aot_descriptor=body->descriptor.c_str();}
          if(body&&body->body==native_boot_method_catalog::Body::kPrimitiveLocal&&!r.storage.boot_unaffected)return Reject("whole-AOT-primitive-source-dependency-owner-not-calibrated");
         }
         // This is only a candidate. No flagless AOT frame is accepted until
         // every map/value and the concrete same-Plan source closure below pass.
         const bool observed_flag=s.flag_||s.family_==Family::kAndroid11||s.family_==Family::kAndroid12;
         unaffected_aot=kind==native_frame_code_owner::Kind::Aot&&physical&&observed_flag&&
           !s.MarkedHeader(this->header)&&r.storage.boot_unaffected;
         if(!unaffected_aot)return Reject(kind==native_frame_code_owner::Kind::Aot?"whole-AOT-requires-actual-frame-exit-or-dependency-closure":"whole-frame-original-async-recovery-refused");
        }else ++r.report.async_frames;
      }
      if(s.family_==Family::kAndroid11?(!s.legacy_fast_&&(!s.legacy_canonical_||!s.debugger_)):
         s.family_==Family::kAndroid12?(!s.fast_&&(!s.legacy_canonical_||!s.debugger_)):(!s.fast_&&(!s.canonical_||!s.debugger_)))return Reject("whole-frame-original-location-reader-unavailable");
      if(!r.recovery&&(!r.jit||!s.jit_||!s.jit_(r.jit,reinterpret_cast<void*>(this->pc))))return Reject("whole-frame-AOT-or-unowned-compiled-code");
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
      if(unaffected_aot&&!native_boot_aot_unaffected::TablesMapped(view,*r.recovery))return Reject("whole-AOT-map-tables-lack-current-stop-extent");
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
          if(s.family_==Family::kAndroid11){
            if(!s.legacy_accessible_||!s.legacy_accessible_(this,own.value,
                 abi::arm64::android11::frame_registers::KindForLocation(actual.kind),&original_register))
              return Reject("whole-frame-original-register-context-unavailable");
          }else if(!s.accessible_||!s.accessible_(this,own.value,actual.kind,&original_register))
            return Reject("whole-frame-original-register-context-unavailable");
          if(core?!r.storage.context->IsAccessibleGPR(own.value):!r.storage.context->IsAccessibleFPR(own.value))return Reject("whole-frame-native-register-context-unavailable");}
        if(actual.kind>=0){
          uint32_t original=0,independent=0;
          const abi::arm64::android12::frame_registers::Location location{actual.kind,actual.value};
          if(s.family_==Family::kAndroid11){
            const auto kind=abi::arm64::android11::frame_registers::KindForLocation(actual.kind);
            // Actual debugger roots must not override the physical snapshot.
            uint32_t overridden=0;
            if(s.debugger_&&s.debugger_(this,static_cast<uint16_t>(index),kind,&overridden))
              return Reject("whole-frame-debugger-shadow-override");
            if(s.legacy_fast_){
              if(!s.legacy_fast_(this,location,kind,&original))return Reject("whole-frame-original-fast-location-read-refused");
            }else if(!s.legacy_canonical_(this,method,static_cast<uint16_t>(index),kind,&original,
                       abi::arm64::android11::frame_registers::OptionalLocation{location}))
              return Reject("whole-frame-original-canonical-location-read-refused");
          }else if(s.fast_){if(!reinterpret_cast<abi::arm64::android12::frame_registers::Fast>(s.fast_)(this,location,&original))return Reject("whole-frame-original-fast-location-read-refused");}
          else if(s.family_==Family::kAndroid12){
            const auto kind=abi::arm64::android11::frame_registers::KindForLocation(actual.kind);
            uint32_t overridden=0;
            if(s.debugger_(this,static_cast<uint16_t>(index),kind,&overridden))return Reject("whole-frame-debugger-shadow-override");
            if(!s.legacy_canonical_(this,method,static_cast<uint16_t>(index),kind,&original,
                 abi::arm64::android11::frame_registers::OptionalLocation{location}))return Reject("whole-frame-original-canonical-location-read-refused");
          }else {uint32_t overridden;
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
         this->header!=original_header||(!r.recovery&&!s.MarkedHeader(original_header))||this->quick_frame!=original_quick||this->pc!=original_pc||
         this->shadow_frame||this->inline_frames.begin.row!=original_begin||this->inline_frames.end.row!=original_end||
         !s.stack_.Method(this,sizeof(*this),&current)||current!=method||s.dex_pc_(this,false)!=pc)return Reject("whole-frame-source-owner-or-native-cursor-lost-after-map");
      if(unaffected_aot){
       if(s.async_.Query(*this->quick_frame,this->pc,*r.recovery)||s.MarkedHeader(this->header)||
          s.code_owner_.Observe(*this->quick_frame,this->pc,this->header,r.jit)!=native_frame_code_owner::Kind::Aot||
          !r.storage.boot_unaffected->Verify(method,pc,view,map.row,range,*r.recovery))return Reject("whole-AOT-current-body-prefix-map-or-dependency-closure-refused");
       ++r.report.unaffected_aot_frames;
      }else if(r.recovery&&!s.async_.Query(*this->quick_frame,this->pc,*r.recovery))return Reject("whole-frame-original-async-recovery-changed-after-map");
      ++r.report.compiled_frames;if(!physical)++r.report.inlined_frames;r.report.map_locations+=range.count;return true;
    }
  };
  template<class Info,class Pair> bool Walk(Request& r,void* target)const {
    r.storage.context->Reset();Visitor<Info,Pair> visitor(r,target);
    if(!stack_.Construct(&visitor,sizeof(visitor),target,r.storage.context,abi::arm64::stack::WalkKind::kIncludeInlined)||
       visitor.thread!=target||visitor.context!=r.storage.context||visitor.kind!=abi::arm64::stack::WalkKind::kIncludeInlined){r.report.failure="whole-thread-original-constructor-geometry-mismatch";return false;}
    if(!stack_.Walk(&visitor,sizeof(visitor),true)){r.report.failure="whole-thread-native-walk-unavailable";return false;}
    if(visitor.runtime_successor){r.report.failure="whole-runtime-original-walker-successor-missing";return false;}
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
  native_frame_async_recovery::Service async_;native_frame_code_owner::Service code_owner_;
  native_runtime_frame::Service runtime_;
  bool compiled_ready_=false;
  Family family_=Family::kAndroid13;stack_visitor_service::Service stack_;
  abi::arm64::context::Create create_=nullptr;void* construct_=nullptr;void* map_=nullptr;
  abi::arm64::dex_register_map::Decode decode_=nullptr;
  uint32_t(*dex_pc_)(const void*,bool)=nullptr;
  abi::arm64::android14::frame_registers::NativePcOffset offset_=nullptr;
  void* fast_=nullptr;
  abi::arm64::android11::frame_registers::Fast legacy_fast_=nullptr;
  abi::arm64::android11::frame_registers::Canonical legacy_canonical_=nullptr;
  abi::arm64::android11::frame_registers::AccessibleRegister legacy_accessible_=nullptr;
  abi::arm64::android14::frame_registers::Canonical canonical_=nullptr;
  abi::arm64::android14::frame_registers::DebuggerValue debugger_=nullptr;
  abi::arm64::android14::frame_registers::AccessibleRegister accessible_=nullptr;
  bool(*jit_)(const void*,const void*)=nullptr;bool(*nterp_)(const void*)=nullptr;bool(*flag_)(const void*)=nullptr;
};
}

inline bool artdcevm::whole_process_frames::StoppedRecoveryProof::Validate(const source_commit::Plan&plan)const noexcept {
 if(!service_||plan_!=&plan||!stop_||!threads_||!context_||!storage_||!stop_->IsStopped(self_)||stop_->IsFinalWindow(self_)!=final_||stop_->IsCollectedStop(self_)!=collected_)return false;
 auto& storage=*static_cast<Service::Prepared*>(storage_);
 if(storage.recovery_generation!=generation_||storage.recovery_plan!=&plan||!source_commit::SameIdentity(storage.recovery_identity,plan))return false;
 // Do not trust a prior scan, listener installation or cached PC: the complete
 // actual thread list and every compiled reader/async leaf are checked again.
 return service_->ScanRecovery(*stop_,self_,*threads_,jit_,storage,plan,*context_).accepted;
}

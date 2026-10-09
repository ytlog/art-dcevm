#pragma once
#include "platform/common/decoded_frame_recovery.h"
#include "platform/common/native_frame_code_owner.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/abi/arm64/context.h"
#include "platform/abi/arm64/android11/frame_registers.h"
#include "platform/abi/arm64/android12/frame_registers.h"
#include "platform/abi/arm64/android13/frame_registers.h"
#include "platform/abi/arm64/android14/frame_registers.h"
#include "platform/abi/arm64/android16/frame_registers.h"
#include "platform/abi/arm64/android15/frame_registers.h"
#include "platform/abi/arm64/android17/frame_registers.h"
#include <memory>
#include "platform/abi/arm64/dex_register_map.h"
#include "platform/abi/arm64/android11/oat_quick_method_header.h"
namespace artdcevm::active_frame_recovery_proof {
using Family=code_info_view::SourceFamily;
using Kind=abi::arm64::android11::frame_registers::Kind;
using NativeLocation=abi::arm64::android11::frame_registers::Location;
enum class Failure {kMissingBinding,kMissingContext,kMissingOracle,kWalk,kMissingFrames,
  kFrameIdentity,kCodeOwnership,kDecodedMap,kTableMemory,kDexPc,kInlineFlag,kReferenceMask,kNativeRead,kValueMismatch,kNone};
struct Report {const char* detail="binding-not-attempted";uint32_t callbacks=0,matched_mask=0,physical_matches=0,inline_matches=0,header_checks=0;
  uint32_t constructor_context_echo_mask=0;
  const char* reader_route="unobserved";
  uint32_t native_location_reads=0,physical_comparison_reads=0,native_context_checks=0,raw_reference_reads=0,native_auxiliary_reads=0,context_identity_mask=0,reference_mask_checks=0;
  bool native_context_getter_observed=false;
  bool native_pc_offset_observed=false;uint64_t native_pc_offset=0,source_pc_offset=0;
  uint32_t map_row=0,inline_begin=0,inline_end=0,native_carrier_registers=0,
           decoded_register_first=0,decoded_register_count=0,expected_registers=0;
  bool register_range_observed=false,native_register_count_observed=false,native_inline_depth_observed=false;
  const char* register_count_source="unobserved";
  uint64_t native_register_count=0;uint32_t slow_register_expected_count=0,source_inline_depth=0,native_inline_depth=0;uint32_t physical_owner_mask=0;
  uint32_t aot_frames=0;Failure failure=Failure::kMissingBinding;uint32_t frames=0,integers=0,references=0,location_reads=0,dex_pc_checks=0,inline_flag_checks=0;};
struct Witness {uint16_t vreg;Kind kind;uint32_t expected;};
struct FrameOracle {void* method;uint32_t registers;const Witness* witnesses;size_t count;uint32_t expected_dex_pc;};
// Exactly one physical carrier and two independently identified inlinees. Raw
// reference expectations must be acquired from strong roots inside the caller's
// exclusive/suspended scope; they are borrowed only during Prove, never stored.
using CodeKind=native_frame_code_owner::Kind;
enum class Shape {kCarrierTwoInlinees,kThreePhysical};
struct Oracle {FrameOracle carrier,inlinees[2];Shape shape=Shape::kCarrierTwoInlinees;CodeKind code=CodeKind::Jit;};
// This proof covers these three independently witnessed physical Java callers.
// It does not grant whole-process frame eligibility or authorize deoptimization.
struct PhysicalOracle {FrameOracle callers[3];CodeKind code=CodeKind::Jit;};
class Service {
 public:
  using Readable=bool (*)(const void*,size_t*);
  template<class Finder> static bool Bind(Family family,Finder finder,Service* output,const char** missing=nullptr) {
    if(missing)*missing=nullptr;
    if(!output)return false;
    Service candidate;candidate.family_=family;candidate.code_owner_=native_frame_code_owner::Service::Bind(finder);
    auto declaration=Declaration(family);if(!declaration)return false;
    auto required=[&](const char* name){void* symbol=finder(name);if(!symbol&&missing&&!*missing)*missing=name;return symbol;};
    const bool stack_bound=stack_visitor_service::Service::Bind(*declaration,required,&candidate.stack_);
    candidate.dex_pc_=reinterpret_cast<DexPc>(required("_ZNK3art12StackVisitor8GetDexPcEb"));
    candidate.create_=reinterpret_cast<abi::arm64::context::Create>(required(abi::arm64::context::kCreate));
    candidate.construct_=required(abi::arm64::android13::kConstructCodeInfo);
    candidate.map_=required(family==Family::kAndroid11?abi::arm64::android11::kFindStackMap:abi::arm64::android13::kFindStackMap);
    candidate.slow_=Modern(family)?finder(abi::arm64::android14::frame_registers::kSlow):required(abi::arm64::android11::frame_registers::kSlow);
    candidate.inline_depth_=reinterpret_cast<InlineDepth>(finder("_ZNK3art12StackVisitor11InlineDepthEv"));
    candidate.register_count_=reinterpret_cast<RegisterCount>(NeedsCountLeaf(family)?
      required("_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi"):
      finder("_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi"));
    candidate.decode_map_=reinterpret_cast<abi::arm64::dex_register_map::Decode>(required(abi::arm64::dex_register_map::kDecode));
    candidate.fast_=finder(family==Family::kAndroid11?abi::arm64::android11::frame_registers::kFast:abi::arm64::android12::frame_registers::kFast);
    candidate.native_offset_=reinterpret_cast<abi::arm64::android14::frame_registers::NativePcOffset>(finder(abi::arm64::android14::frame_registers::kNativePcOffset));
    candidate.accessible_=reinterpret_cast<abi::arm64::android14::frame_registers::AccessibleRegister>(finder(abi::arm64::android14::frame_registers::kAccessibleRegister));
    if(Modern(family)&&!candidate.fast_) {
      candidate.canonical_=reinterpret_cast<abi::arm64::android14::frame_registers::Canonical>(required(abi::arm64::android14::frame_registers::kCanonical));
      candidate.debugger_=reinterpret_cast<abi::arm64::android14::frame_registers::DebuggerValue>(required(abi::arm64::android14::frame_registers::kDebuggerValue));
    }
    candidate.jit_contains_=reinterpret_cast<ContainsJit>(required("_ZNK3art3jit12JitCodeCache10ContainsPcEPKv"));
    candidate.header_contains_=reinterpret_cast<ContainsHeader>(finder("_ZNK3art20OatQuickMethodHeader8ContainsEm"));
    candidate.nterp_=family==Family::kAndroid11?nullptr:reinterpret_cast<Nterp>(required("_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv"));
    if(!candidate.dex_pc_ || !candidate.create_ || !candidate.construct_ || !candidate.map_ || (!Modern(family) && !candidate.slow_) || (Modern(family) && !candidate.fast_ && (!candidate.canonical_ || !candidate.debugger_)) || !candidate.decode_map_ || (NeedsCountLeaf(family) && !candidate.register_count_) ||
       !stack_bound || !candidate.jit_contains_ || (family!=Family::kAndroid11 && !candidate.nterp_))return false;
    *output=candidate;return true;
  }
  // Call ONLY on a genuinely suspended target under ART's required mutator lock,
  // with independently established source object ABIs and strong expected roots.
  // Named availability alone never establishes those prerequisites. This entry
  // walks real native objects and creates its own original architecture Context.
  // No JNI, suspension, capabilities or frame/register mutation is performed.
  Report Prove(void* thread,void* jit,const Oracle& oracle,
               decoded_frame_recovery::Location* scratch,size_t capacity,Readable readable=nullptr) const {
    Report report;
    if(!create_)return report;
    if(!thread || (oracle.code==CodeKind::Jit&&!jit) || !scratch || !Valid(oracle)) {report.failure=Failure::kMissingOracle;report.detail="missing-independent-inputs";return report;}
    std::unique_ptr<abi::arm64::context::Prefix> context(create_());
    if(!context){report.failure=Failure::kMissingContext;report.detail="native-context-create-returned-null";return report;}
    report.detail="context-created";
    return ProvePrepared(thread,jit,oracle,scratch,capacity,context.get(),readable);
  }
  // Production calibration prepares Context and scratch before the owned stop.
  // This borrowed path performs no allocation/deletion or Context creation.
  Report ProvePrepared(void* thread,void* jit,const Oracle& oracle,
      decoded_frame_recovery::Location* scratch,size_t capacity,
      abi::arm64::context::Prefix* context,Readable readable)const {
    Report report;
    if(!context){report.failure=Failure::kMissingContext;return report;}
    if(!thread||(oracle.code==CodeKind::Jit&&!jit)||!scratch||!Valid(oracle)){
      report.failure=Failure::kMissingOracle;return report;
    }
    context->Reset(); // WalkStack then fills native callee-saves while unwinding.
    switch(family_) {
      case Family::kAndroid11:Walk<abi::arm64::android11::CodeInfo,abi::arm64::android11::CodeInfoPair>(thread,jit,context,oracle,scratch,capacity,readable,&report);break;
      case Family::kAndroid12:Walk<abi::arm64::android12::CodeInfo,abi::arm64::android12::CodeInfoPair>(thread,jit,context,oracle,scratch,capacity,readable,&report);break;
      case Family::kAndroid13:case Family::kAndroid14:case Family::kAndroid15:case Family::kAndroid16:
        Walk<abi::arm64::android13::CodeInfo,abi::arm64::android13::CodeInfoPair>(thread,jit,context,oracle,scratch,capacity,readable,&report);break;
      case Family::kAndroid17:Walk<abi::arm64::android17::CodeInfo,abi::arm64::android17::CodeInfoPair>(thread,jit,context,oracle,scratch,capacity,readable,&report);break;
    }
    return report;
  }
  Report ProvePhysical(void* thread,void* jit,const PhysicalOracle& physical,
      decoded_frame_recovery::Location* scratch,size_t capacity,Readable readable=nullptr) const {
    const Oracle oracle{physical.callers[0],{physical.callers[1],physical.callers[2]},Shape::kThreePhysical,physical.code};
    return Prove(thread,jit,oracle,scratch,capacity,readable);
  }
 private:
  using InlineDepth=size_t (*)(const void*);
  using RegisterCount=size_t (*)(const void*,void*,int);
  using DexPc=uint32_t (*)(const void*,bool);
  static uint32_t InlineMask(Family f) {
    switch(f){
      case Family::kAndroid11:return abi::arm64::android11::kHasInlineInfo;
      case Family::kAndroid12:return abi::arm64::android12::kHasInlineInfo;
      case Family::kAndroid13:return abi::arm64::android13::kHasInlineInfo;
      case Family::kAndroid14:return abi::arm64::android14::kHasInlineInfo;
      case Family::kAndroid15:return abi::arm64::android15::kHasInlineInfo;
      case Family::kAndroid16:return abi::arm64::android16::kHasInlineInfo;
      case Family::kAndroid17:return abi::arm64::android17::kHasInlineInfo;
    }return 0;
  }
  using ContainsJit=bool (*)(const void*,const void*);
  using ContainsHeader=bool (*)(const void*,uintptr_t);
  using Nterp=bool (*)(const void*);
  static bool NeedsCountLeaf(Family f){return f==Family::kAndroid14 || f==Family::kAndroid15 || f==Family::kAndroid16;}
  static bool Modern(Family f){return f==Family::kAndroid14 || f==Family::kAndroid15 || f==Family::kAndroid16 || f==Family::kAndroid17;}
  static std::optional<stack_visitor_service::Declaration> Declaration(Family f) {
    using D=stack_visitor_service::Declaration;
    switch(f){case Family::kAndroid11:return D::kAndroid11;case Family::kAndroid12:return D::kAndroid12;
      case Family::kAndroid13:case Family::kAndroid14:case Family::kAndroid15:case Family::kAndroid16:return D::kAndroid13To16;
      case Family::kAndroid17:return D::kAndroid17;}
    return std::nullopt;
  }
  static bool Valid(const Oracle& o) {
    if(o.shape!=Shape::kCarrierTwoInlinees && o.shape!=Shape::kThreePhysical)return false;
    const FrameOracle* frames[]={&o.carrier,&o.inlinees[0],&o.inlinees[1]};
    for(size_t i=0;i<3;++i){const auto& f=*frames[i];
      if(f.expected_dex_pc==UINT32_MAX || !f.method || !f.registers || !f.witnesses || f.count<2)return false;
      bool integer=false,reference=false;
      for(size_t w=0;w<f.count;++w){const auto& witness=f.witnesses[w];
        if(witness.vreg>=f.registers || (witness.kind!=Kind::kInt && witness.kind!=Kind::kReference))return false;
        for(size_t p=0;p<w;++p)if(f.witnesses[p].vreg==witness.vreg)return false;
        integer|=witness.kind==Kind::kInt;
        reference|=witness.kind==Kind::kReference && witness.expected!=0;
      }
      if(!integer || !reference)return false;
      for(size_t p=0;p<i;++p) {
        if(frames[p]->method==f.method)return false;
        // Independent fixtures must distinguish every frame; equal witnesses
        // could hide a mis-decoded inline range or carrier alias.
        for(size_t w=0;w<f.count;++w)for(size_t prior=0;prior<frames[p]->count;++prior)
          if(f.witnesses[w].kind==frames[p]->witnesses[prior].kind &&
             f.witnesses[w].expected==frames[p]->witnesses[prior].expected)return false;
      }
    }
    return true;
  }
  bool NativeSlow(const void* visitor,void* method,const Witness& witness,uint32_t* value)const {
    if(Modern(family_))return reinterpret_cast<abi::arm64::android14::frame_registers::Slow>(slow_)(visitor,method,witness.vreg,witness.kind,value,false);
    return reinterpret_cast<abi::arm64::android11::frame_registers::Slow>(slow_)(visitor,method,witness.vreg,witness.kind,value);
  }
  bool NativeFast(const void* visitor,decoded_frame_recovery::Location location,Kind kind,uint32_t* value)const {
    if(!fast_)return false;
    NativeLocation native{location.kind,static_cast<int32_t>(location.value)};
    if(family_==Family::kAndroid11)return reinterpret_cast<abi::arm64::android11::frame_registers::Fast>(fast_)(visitor,native,kind,value);
    return reinterpret_cast<abi::arm64::android12::frame_registers::Fast>(fast_)(visitor,native,value);
  }
  bool OriginalValue(const void* visitor,void* method,const Witness& witness,
                     decoded_frame_recovery::Location location,uint32_t* value)const {
    if(!Modern(family_))return NativeSlow(visitor,method,witness,value);
    if(fast_)return NativeFast(visitor,location,witness.kind,value);
    if(!canonical_||!debugger_)return false;
    // The source canonical fast branch accepts a known location for primitive
    // reads. Reference slots are read as raw compressed32 here, not dereferenced
    // and not represented as a read-barrier call. Their separate GC/root proof
    // is mandatory. Reject debugger overrides so this is the native location
    // branch, rather than a shadow-frame oracle keyed only by the vreg.
    uint32_t shadow=0;
    if(debugger_(visitor,witness.vreg,Kind::kInt,&shadow))return false;
    const NativeLocation native{location.kind,static_cast<int32_t>(location.value)};
    return canonical_(visitor,method,witness.vreg,Kind::kInt,value,
                      abi::arm64::android14::frame_registers::OptionalLocation{native},false);
  }
  // Fixed StackVisitor location-reader algorithm. Stack displacement is from
  // the actual native-decoded map and is bounded by its decoded frame size at
  // the call site, not an inferred object offset. Register storage is read only
  // through the original Context virtual API, after accessibility checks.
  static bool Physical(void** frame,abi::arm64::context::Prefix* context,
                       decoded_frame_recovery::Location location,uint32_t* value) {
    if(location.kind==0){std::memcpy(value,reinterpret_cast<const uint8_t*>(frame)+location.value,4);return true;}
    if(location.kind==1){*value=location.value;return true;}
    const bool core=location.kind==2 || location.kind==3;
    const bool fpu=location.kind==4 || location.kind==5;
    if(!context || (!core && !fpu) || location.value>=32)return false;
    if(core && !context->IsAccessibleGPR(location.value))return false;
    if(fpu && !context->IsAccessibleFPR(location.value))return false;
    uintptr_t raw=core?context->GetGPR(location.value):context->GetFPR(location.value);
    if(location.kind==3 || location.kind==5)raw>>=32;
    *value=static_cast<uint32_t>(raw);return true;
  }
  static bool RootMask(const code_info_view::View& info,uint32_t row,decoded_frame_recovery::Location location) {
    uint32_t index;
    if(location.kind==0) {bool root;
      return info.StackMaps().Read(row,4,&index) && index!=UINT32_MAX &&
        decoded_frame_recovery::MaskBit(info.tables[2],index,location.value/4,&root) && root;
    }
    if(location.kind!=2 || location.value>=32 || !info.StackMaps().Read(row,3,&index) || index==UINT32_MAX)return false;
    uint32_t value,shift;
    return info.tables[1].Read(index,0,&value) && info.tables[1].Read(index,1,&shift) && shift<32 &&
      value<=UINT32_MAX>>shift && ((value<<shift)&(1u<<location.value));
  }
  template<class Info,class Pair> struct Visitor final:abi::arm64::stack::Visitor<Pair> {
    // Reserve the largest supported fixed native base before callback state.
    // Candidate constructor geometry is checked against its own Context/thread
    // echo before walking, so smaller source candidates cannot corrupt state.
    unsigned char native_tail[sizeof(abi::arm64::stack::Visitor12)-sizeof(abi::arm64::stack::Visitor<Pair>)+8]{};
    const Service& service;void* jit;void* original_context;const Oracle& oracle;decoded_frame_recovery::Location* scratch;size_t capacity;Readable readable;Report& report;
    void** physical_frames[3]{};void** carrier=nullptr;const void* shared_header=nullptr;uintptr_t shared_pc=0;uint32_t seen=0,inline_rows[2]{};
    Visitor(const Service& s,void* j,void* original,const Oracle& o,decoded_frame_recovery::Location* storage,size_t n,Readable reader,Report& r):abi::arm64::stack::Visitor<Pair>{},service(s),jit(j),original_context(original),oracle(o),scratch(storage),capacity(n),readable(reader),report(r){}
    bool Fail(Failure failure,const char* detail){report.failure=failure;report.detail=detail;return false;}
    bool VisitFrame()override {
      ++report.callbacks;
      void* method=nullptr;if(!service.stack_.Method(this,sizeof(*this),&method))return Fail(Failure::kWalk,"named-GetMethod-call-unavailable");
      const FrameOracle* f=nullptr;uint32_t bit=0;
      if(method==oracle.carrier.method){f=&oracle.carrier;bit=1;}
      for(size_t i=0;i<2;++i)if(method==oracle.inlinees[i].method){f=&oracle.inlinees[i];bit=2u<<i;}
      if(!f)return true;
      report.matched_mask|=bit;
      const bool physical=this->inline_frames.begin.row==this->inline_frames.end.row;
      if(physical)++report.physical_matches;else ++report.inline_matches;
      if(oracle.shape==Shape::kThreePhysical && bit!=(seen==0?4u:seen==4?2u:1u))
        return Fail(Failure::kFrameIdentity,"physical-caller-chain-order-mismatch");
      if(seen&bit)return Fail(Failure::kFrameIdentity,"duplicate-owned-method-callback");
      // Debuggable graphs cannot inline in fixed HInliner::Run. Observe these
      // real physical inlinee frames, but never count them as inline proof.
      if(oracle.shape==Shape::kCarrierTwoInlinees && bit!=1 && physical){report.detail="owned-inlinee-observed-as-separate-physical-frame";return true;}
      if((bit==1 || oracle.shape==Shape::kThreePhysical) && !physical)return Fail(Failure::kFrameIdentity,"owned-carrier-observed-as-inline-frame");
      if(!this->quick_frame)return Fail(Failure::kFrameIdentity,"owned-frame-has-no-quick-frame");
      if(!this->header)return Fail(Failure::kFrameIdentity,"owned-frame-has-no-quick-header");
      if(!this->context||this->context!=original_context)return Fail(Failure::kFrameIdentity,"owned-frame-does-not-retain-original-native-context");
      report.context_identity_mask|=bit;report.native_context_getter_observed=service.accessible_!=nullptr;
      if(service.accessible_) {
        auto* context=static_cast<abi::arm64::context::Prefix*>(this->context);
        for(uint32_t reg=0;reg<32;++reg)for(int32_t kind:{2,4}) {
          const bool available=kind==2?context->IsAccessibleGPR(reg):context->IsAccessibleFPR(reg);
          uint32_t native=0;
          if(service.accessible_(this,reg,kind,&native)!=available)
            return Fail(Failure::kFrameIdentity,"original-visitor-versus-native-context-accessibility-mismatch");
          if(available) {
            const uint32_t expected=static_cast<uint32_t>(kind==2?context->GetGPR(reg):context->GetFPR(reg));
            if(native!=expected)return Fail(Failure::kFrameIdentity,"original-visitor-versus-native-context-value-mismatch");
            ++report.native_context_checks;
          }
        }
      }
      if(!physical) {
        const size_t index=bit==2?0:1,other=1-index;
        if((seen&(2u<<other)) && inline_rows[other]==this->inline_frames.end.row)return Fail(Failure::kFrameIdentity,"duplicate-inline-row");
        inline_rows[index]=this->inline_frames.end.row;
      }
      if(oracle.shape==Shape::kCarrierTwoInlinees) {
        if(!carrier){carrier=this->quick_frame;shared_header=this->header;shared_pc=this->pc;}
        if(carrier!=this->quick_frame)return Fail(Failure::kFrameIdentity,"inlinees-do-not-share-physical-frame");
        if(shared_header!=this->header || shared_pc!=this->pc)return Fail(Failure::kFrameIdentity,"inlinees-do-not-share-header-pc");
        report.physical_owner_mask=*carrier==oracle.carrier.method?1u:
          *carrier==oracle.inlinees[0].method?2u:*carrier==oracle.inlinees[1].method?4u:0u;
        if(report.physical_owner_mask!=1)return Fail(Failure::kFrameIdentity,"quick-frame-owner-is-not-independent-carrier");
      } else {
        const size_t index=bit==1?0:bit==2?1:2;
        for(auto* frame:physical_frames)if(frame==this->quick_frame)
          return Fail(Failure::kFrameIdentity,"physical-callers-share-quick-frame");
        physical_frames[index]=this->quick_frame;
        if(*this->quick_frame!=f->method)
          return Fail(Failure::kFrameIdentity,"quick-frame-owner-is-not-independent-physical-caller");
        report.physical_owner_mask|=bit;
      }
      const auto code=service.code_owner_.Observe(*this->quick_frame,this->pc,this->header,jit);
      if((oracle.code!=CodeKind::Jit&&oracle.code!=CodeKind::Aot)||code!=oracle.code ||
         (service.header_contains_ && !service.header_contains_(this->header,this->pc)) || (service.nterp_ && service.nterp_(this->header)))return Fail(Failure::kCodeOwnership,"native-code-owner-header-PC-kind-mismatch");
      if(code==CodeKind::Aot)++report.aot_frames;
      // Fixed native header prefixes put code at byte8 on11 and byte4
      // thereafter. Native PC ownership above and the actual native stack-map
      // lookup/value witnesses must agree with that caller-owned declaration.
      const uintptr_t entry=reinterpret_cast<uintptr_t>(this->header)+(service.family_==Family::kAndroid11?8u:4u);
      if(this->pc<entry || (service.family_==Family::kAndroid11 && this->pc-entry>UINT32_MAX))return Fail(Failure::kCodeOwnership,"native-jit-header-pc-or-nterp-ownership-mismatch");
      const uintptr_t source_offset=this->pc-entry;
      const uintptr_t native_offset=service.native_offset_?service.native_offset_(this):source_offset;
      report.native_pc_offset_observed=service.native_offset_!=nullptr;
      report.native_pc_offset=native_offset;report.source_pc_offset=source_offset;
      if(native_offset!=source_offset)return Fail(Failure::kCodeOwnership,"original-native-PC-offset-versus-fixed-header-mismatch");
      // Reserve every supported fixed decoder object while testing a candidate.
      // Geometry still comes only from Info and independent frame witnesses.
      alignas(Info) unsigned char storage[sizeof(abi::arm64::android12::CodeInfo)]{};
      auto* info=reinterpret_cast<Info*>(storage);
      reinterpret_cast<void (*)(Info*,const void*)>(service.construct_)(info,this->header);
      abi::arm64::android11::TableIterator map;
      if(service.family_==Family::kAndroid11)map=reinterpret_cast<abi::arm64::android11::FindStackMap>(service.map_)(reinterpret_cast<abi::arm64::android11::CodeInfo*>(info),static_cast<uint32_t>(native_offset),2);
      else map=reinterpret_cast<abi::arm64::android11::TableIterator (*)(const Info*,uintptr_t,int32_t)>(service.map_)(info,native_offset,2);
      if(map.table!=&info->stack_maps)return Fail(Failure::kDecodedMap,"native-stack-map-table-does-not-match-fixed-declaration");
      code_info_view::View view;decoded_frame_recovery::RegisterRange range;
      if(!code_info_view::ReadSourceObject(service.family_,info,sizeof(storage),&view))return Fail(Failure::kDecodedMap,"native-decoded-CodeInfo-table-geometry-invalid");
      // Fixed12+ Contains uses GetCode()+native decoded CodeInfo code_size.
      // On12 the inline function has no named ELF body. This source equivalent
      // stays independent of map/value checks and of actual Jit ContainsPc.
      if(!service.header_contains_) {
        if(service.family_==Family::kAndroid11) {
          size_t extent=sizeof(abi::arm64::android11::OatQuickMethodHeaderPrefix);
          if((readable && !readable(this->header,&extent)) ||
             !abi::arm64::android11::OptimizedHeaderContains(this->header,extent,this->pc))
            return Fail(Failure::kCodeOwnership,"fixed11-optimized-header-does-not-contain-native-pc");
        } else if(!view.code_size || !*view.code_size || this->pc-entry>*view.code_size)
          return Fail(Failure::kCodeOwnership,"decoded-code-size-does-not-contain-native-pc");
      }
      ++report.header_checks;
      if(readable)for(const auto& table:view.tables) {
        const size_t bits=size_t{table.rows}*table.row_bits;
        if(!bits)continue;
        const size_t bytes=(table.region.bit_start+bits-1)/8+1;size_t extent=0;
        if(!readable(table.region.data,&extent)||extent<bytes)return Fail(Failure::kTableMemory,"native-table-storage-not-fully-readable");
      }
      // Original WalkStack emits inline callbacks only with native CodeInfo and
      // StackMap HasInlineInfo. Compare precisely this flag, never other flags.
      if(!physical) {
        if(!(view.flags&InlineMask(service.family_)))return Fail(Failure::kInlineFlag,"native-inline-callback-versus-decoded-inline-flag-mismatch");
        ++report.inline_flag_checks;
      }
      uint32_t decoded_pc;
      const bool decoded=physical?view.StackMaps().Read(map.row,2,&decoded_pc):
        view.InlineInfos().Read(this->inline_frames.end.row-1,1,&decoded_pc);
      if(!decoded || decoded_pc!=f->expected_dex_pc ||
         service.dex_pc_(this,false)!=f->expected_dex_pc)return Fail(Failure::kDexPc,"native-decoded-independent-invoke-DexPc-mismatch");
      ++report.dex_pc_checks;
      report.map_row=map.row;report.inline_begin=this->inline_frames.begin.row;
      report.inline_end=this->inline_frames.end.row;report.native_carrier_registers=view.dex_regs;
      report.expected_registers=f->registers;report.register_range_observed=false;
      uint32_t register_map;
      if(!view.StackMaps().Read(map.row,7,&register_map))
        return Fail(Failure::kDecodedMap,"native-dex-register-map-index-unreadable");
      if(register_map==UINT32_MAX)
        return Fail(Failure::kDecodedMap,"native-stack-map-has-no-dex-register-map");
      if(!decoded_frame_recovery::Range(view,map.row,report.inline_begin,report.inline_end,&range))
        return Fail(Failure::kDecodedMap,"decoded-inline-register-range-invalid");
      report.register_range_observed=true;report.decoded_register_first=range.first;
      report.decoded_register_count=range.count;
      if(range.count!=f->registers)
        return Fail(Failure::kDecodedMap,"decoded-vreg-count-does-not-match-actual-CodeItem");
      if(!decoded_frame_recovery::Decode(view,map.row,report.inline_begin,report.inline_end,scratch,capacity,&range))
        return Fail(Failure::kDecodedMap,"decoded-vreg-locations-invalid");
      // Match the original slow reader's own register-count leaf and depth
      // contract, rather than inferring its bounds from our map range alone.
      if(service.register_count_ || service.family_==Family::kAndroid17) {
        const uint32_t depth=this->inline_frames.end.row-this->inline_frames.begin.row;
        report.source_inline_depth=depth;report.slow_register_expected_count=range.first+range.count;
        if(service.inline_depth_) {
          const size_t native_depth=service.inline_depth_(this);
          report.native_inline_depth_observed=true;
          report.native_inline_depth=static_cast<uint32_t>(native_depth);
          if(native_depth!=depth)return Fail(Failure::kFrameIdentity,"original-inline-depth-versus-source-cursor-mismatch");
        }
        if(depth>INT32_MAX)return Fail(Failure::kDecodedMap,"native-inline-depth-outside-source-register-count-contract");
        size_t native_count;
        if(service.register_count_) {
          native_count=service.register_count_(this,info,static_cast<int>(depth));
          report.native_register_count_observed=true;report.register_count_source="named-original-GetNumberOfRegisters";
        }else {
          // Exact b753cf stack.cc helper: physical depth0 is the complete
          // native-constructed CodeInfo count; inline depth selects the current
          // native range's last cumulative register count. The helper body may
          // be compiler-inlined and absent from the actual named ELF inventory.
          uint32_t source_count=view.dex_regs;
          if(depth && !view.InlineInfos().Read(this->inline_frames.end.row-1,5,&source_count))
            return Fail(Failure::kDecodedMap,"fixed17-slow-cumulative-register-count-unreadable");
          native_count=source_count;report.register_count_source="fixed17-original-helper-equivalent";
        }
        report.native_register_count=native_count;
        const size_t expected_count=range.first+range.count;
        if(native_count!=expected_count)
          return Fail(Failure::kDecodedMap,"original-slow-register-count-versus-complete-map-mismatch");
      }
      // Independently call the exact native decoder with its complete source
      // container, before the slow reader's release-build unchecked map[vreg].
      // Compare ALL locations, including missing registers, not only witnesses.
      abi::arm64::dex_register_map::Map native_map(range.count);
      service.decode_map_(info,map.row,range.first,&native_map);
      if(native_map.count!=range.count)
        return Fail(Failure::kDecodedMap,"native-dex-register-map-count-changed");
      for(uint32_t reg=0;reg<range.count;++reg) {
        const auto native=native_map.data()[reg];const auto own=scratch[reg];
        if(native.kind!=own.kind || (native.kind>=0 && static_cast<uint32_t>(native.value)!=own.value))
          return Fail(Failure::kDecodedMap,"native-versus-source-complete-register-map-mismatch");
      }
      if(info->frame_size>UINT32_MAX/16)return Fail(Failure::kDecodedMap,"decoded-frame-size-overflow");
      const uint32_t bytes=info->frame_size*16;
      for(size_t i=0;i<f->count;++i){const auto& witness=f->witnesses[i];const auto location=scratch[witness.vreg];
        if(location.kind<0 || (location.kind==0 && (bytes<4 || location.value>bytes-4)))return Fail(Failure::kDecodedMap,"decoded-location-missing-or-outside-native-frame");
        if(witness.kind==Kind::kReference) {
          if(!RootMask(view,map.row,location))return Fail(Failure::kReferenceMask,"independent-reference-missing-native-stack-register-root-mask");
          ++report.reference_mask_checks;
        }
        void* current_method=nullptr;
        const void* current_header=this->header;void** current_frame=this->quick_frame;
        void* current_context=this->context;
        const uintptr_t current_pc=this->pc;const auto current_range=this->inline_frames;
        if(!service.stack_.Method(this,sizeof(*this),&current_method) || current_method!=method ||
           service.dex_pc_(this,false)!=f->expected_dex_pc || this->header!=current_header ||
           this->quick_frame!=current_frame || this->pc!=current_pc || this->context!=current_context ||
           this->inline_frames.begin.row!=current_range.begin.row ||
           this->inline_frames.end.row!=current_range.end.row ||
           (physical && *this->quick_frame!=method))
          return Fail(Failure::kFrameIdentity,"native-current-cursor-changed-before-vreg-read");
        uint32_t value=0;
        if(!service.OriginalValue(this,method,witness,location,&value))return Fail(Failure::kNativeRead,"original-native-location-read-rejected");
        ++report.native_location_reads;
        report.reader_route=Modern(service.family_)?(service.fast_?"original-native-location-fast":"original-canonical-optional-location-fast"):"original-native-slow";
        if(value!=witness.expected)return Fail(Failure::kValueMismatch,"original-native-value-versus-independent-witness-mismatch");
        if(!Modern(service.family_)&&service.fast_) {
          uint32_t auxiliary=0;
          if(!service.NativeFast(this,location,witness.kind,&auxiliary))return Fail(Failure::kNativeRead,"original-legacy-fast-read-rejected");
          ++report.native_auxiliary_reads;
          if(auxiliary!=value)return Fail(Failure::kValueMismatch,"original-legacy-fast-versus-slow-reader-mismatch");
        }
        if(this->context!=current_context||this->quick_frame!=current_frame||this->header!=current_header||this->pc!=current_pc)
          return Fail(Failure::kFrameIdentity,"native-reader-changed-current-cursor-or-context");
        uint32_t physical=0;
        if(!Physical(this->quick_frame,static_cast<abi::arm64::context::Prefix*>(this->context),location,&physical))return Fail(Failure::kNativeRead,"independent-physical-location-reader-unavailable");
        ++report.physical_comparison_reads;
        if(physical!=value)return Fail(Failure::kValueMismatch,"physical-location-versus-original-native-reader-mismatch");
        if(witness.kind==Kind::kReference)++report.raw_reference_reads;
        ++report.location_reads;
        if(witness.kind==Kind::kInt)++report.integers;
        else ++report.references;
      }
      if(service.code_owner_.Observe(*this->quick_frame,this->pc,this->header,jit)!=code)return Fail(Failure::kCodeOwnership,"native-code-owner-changed-after-value-read");
      seen|=bit;++report.frames;return seen!=7;
    }
  };
  template<class Info,class Pair> void Walk(void* thread,void* jit,void* context,const Oracle& oracle,
      decoded_frame_recovery::Location* scratch,size_t capacity,Readable readable,Report* report)const {
    Visitor<Info,Pair> visitor(*this,jit,context,oracle,scratch,capacity,readable,*report);
    if(!stack_.Construct(&visitor,sizeof(visitor),thread,context,abi::arm64::stack::WalkKind::kIncludeInlined)) {report->failure=Failure::kWalk;report->detail="native-visitor-construct-or-walk-call-unavailable";return;}
    report->constructor_context_echo_mask=abi::arm64::stack::ConstructorContextEchoMask(&visitor,sizeof(visitor),context);
    if(visitor.thread!=thread || visitor.context!=context || visitor.kind!=abi::arm64::stack::WalkKind::kIncludeInlined) {
      report->failure=Failure::kFrameIdentity;report->detail=visitor.thread!=thread?"constructor-thread-echo-mismatch":visitor.context!=context?"constructor-context-echo-mismatch":"constructor-walk-kind-echo-mismatch";return;
    }
    report->detail="constructor-echo-matched-walk-started";
    if(!stack_.Walk(&visitor,sizeof(visitor))) {report->failure=Failure::kWalk;report->detail="native-visitor-construct-or-walk-call-unavailable";return;}
    if(visitor.seen==7){report->failure=Failure::kNone;report->detail=oracle.shape==Shape::kThreePhysical?"complete-owned-three-physical-proof":"complete-owned-physical-two-inline-proof";}
    else if(report->failure==Failure::kMissingBinding){report->failure=Failure::kMissingFrames;report->detail=oracle.shape==Shape::kThreePhysical?"missing-owned-three-physical-callers":report->physical_matches>1?"missing-actual-inlinees-physical-fixture-frames-observed":"missing-owned-carrier-or-two-inline-callbacks";}
  }
  Family family_=Family::kAndroid13;stack_visitor_service::Service stack_;
  abi::arm64::context::Create create_=nullptr;void* construct_=nullptr;void* map_=nullptr;void* slow_=nullptr;void* fast_=nullptr;
  abi::arm64::android14::frame_registers::Canonical canonical_=nullptr;
  abi::arm64::android14::frame_registers::DebuggerValue debugger_=nullptr;
  abi::arm64::android14::frame_registers::NativePcOffset native_offset_=nullptr;
  abi::arm64::android14::frame_registers::AccessibleRegister accessible_=nullptr;
  InlineDepth inline_depth_=nullptr;
  RegisterCount register_count_=nullptr;
  abi::arm64::dex_register_map::Decode decode_map_=nullptr;
  native_frame_code_owner::Service code_owner_;
  DexPc dex_pc_=nullptr;ContainsJit jit_contains_=nullptr;ContainsHeader header_contains_=nullptr;Nterp nterp_=nullptr;
};
}

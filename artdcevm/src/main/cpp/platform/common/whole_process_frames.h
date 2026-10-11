#include "platform/common/native_frame_root_binding.h"
#include "platform/common/native_boot_method_catalog.h"
#pragma once
#include "platform/common/native_exception_upcall_boundary.h"
#include "platform/common/native_jni_exception_calibration.h"
#include "platform/common/native_exception_entry_ownership.h"
#include "platform/common/native_boot_aot_unaffected.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/stack_visitor_service.h"
#include "platform/common/decoded_frame_recovery.h"
#include "platform/common/native_frame_async_recovery.h"
#include "platform/common/native_frame_code_owner.h"
#include "platform/common/native_runtime_frame.h"
#include "platform/common/native_frame_root_relocation.h"
#include "platform/common/native_callee_edge.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_receiver_producer.h"
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
class FrameRootBinding;
class FrameRootBinding final:public RecaptureInterface {
 public:
  FrameRootBinding()=default;
  FrameRootBinding(const FrameRootBinding&)=delete;FrameRootBinding&operator=(const FrameRootBinding&)=delete;
  FrameRootBinding(FrameRootBinding&&)=delete;FrameRootBinding&operator=(FrameRootBinding&&)=delete;
  void Clear()noexcept {service_=nullptr;stop_=nullptr;threads_=nullptr;self_=nullptr;storage_=nullptr;plan_=nullptr;read_=nullptr;proof_=nullptr;}
  bool Recapture(bool after)const noexcept override;
 private:
  friend class Service;
  const Service* service_=nullptr; const native_source_stop::Session* stop_=nullptr;
  const native_cache::Service* threads_=nullptr; void*self_=nullptr;void*storage_=nullptr;
  const source_commit::Plan*plan_=nullptr;const modern_field_metadata::ReadContext*read_=nullptr;
  native_frame_root_relocation::Interface*proof_=nullptr;
};
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
 uint32_t failure_map_row=UINT32_MAX,failure_map_mask=UINT32_MAX,failure_map_index=UINT32_MAX,
   failure_map_dex_regs=0,failure_map_dex_pc=UINT32_MAX,failure_inline_begin=0,failure_inline_end=0;
 size_t failure_map_capacity=0;
 uint32_t failure_frame_bytes=0,failure_stack_bit=UINT32_MAX;
 char runtime_name[128]{};
 uint32_t runtime_name_status=0;uintptr_t runtime_name_thread=0,runtime_name_method=0,runtime_name_quick=0;
 // Copied diagnostics only; these fields grant no receiver or dispatch authority.
 bool younger_present=false;
 uintptr_t receiver_caller_thread=0,receiver_caller_method=0,receiver_caller_quick=0,receiver_caller_pc=0;
 uintptr_t younger_method=0,younger_quick=0,younger_pc=0,receiver_slot=0,receiver_value=0;
 uint32_t previous_kind=5,receiver_state=0,receiver_registers=0,receiver_ins=0,
   receiver_this_index=UINT32_MAX,receiver_map_first=0,receiver_map_count=0;
 int32_t receiver_location_kind=-1;uint32_t receiver_location_value=0;
 uint32_t root_failure=UINT32_MAX,root_stream_failure=UINT32_MAX;
 size_t root_frames=0,root_slots=0,root_callbacks=0;
 bool failure_method_prefix=false,failure_physical=false;
 const char* aot_declaring_class=nullptr;const char* aot_method=nullptr;const char* aot_descriptor=nullptr;
 size_t registered_threads=0,walked_threads=0,empty_threads=0,frames=0,transitions=0,
   async_frames=0,aot_frames=0,unaffected_aot_frames=0,shadow_frames=0,native_frames=0,runtime_frames=0,compiled_frames=0,inlined_frames=0,map_locations=0,native_value_reads=0;
 const char* aot_verify_failure=nullptr;
 uintptr_t aot_verify_thread=0,aot_verify_method=0;uint32_t aot_verify_dex_pc=UINT32_MAX;
 const char* failure="not-scanned";
 size_t upcall_threads=0,upcall_closed=0,upcall_fragments=0,upcall_transitions=0;
 uint32_t upcall_failure=UINT32_MAX;
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
    c.upcall_frame_info_=reinterpret_cast<native_runtime_frame::FrameInfo(*)(const void*)>(finder(native_runtime_frame::Service::kFrameInfo));
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
  struct Census {size_t frames=0,logical_frames=0,slots=0,code_units=0,tries=0,handlers=0,threads=0,managed_fragments=0;bool accepted=false;const char*failure="not-counted";Report diagnostic{};};
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
    native_frame_root_relocation::Interface* frame_roots=nullptr;
    const native_typed_heap_walk::RootProvenance* root_provenance=nullptr;
    const prepared_roots::Resolver* frame_root_resolver=nullptr;
    const native_interface_dispatch::Proof* dispatch=nullptr;
    const native_boot_aot_unaffected::Proof* continuation=nullptr;
    const receiver_guard::StoppedCodeReader* source_code=nullptr;
    const source_handle_roots::Service* managed_stack_source=nullptr;
    const native_jni_exception_calibration::Service* exception_calibration=nullptr;
    JavaVM* exception_vm=nullptr;
    const native_exception_entry_ownership::Proof* upcall_native_owner=nullptr;
    struct Upcall {native_managed_stack_chain::Chain chain;native_exception_upcall_boundary::Proof boundary;void*thread=nullptr;bool exhausted=false;};
    std::vector<std::unique_ptr<Upcall>> upcalls;
    size_t upcalls_used=0;
    struct Pending {native_frame_root_relocation::Frame frame{};uint32_t dex_pc=0;bool aot=false,boundary=false;native_callee_edge::Kind kind=native_callee_edge::Kind::Unknown;
      native_callee_edge::Frame younger{};const void* receiver_address=nullptr;void*receiver=nullptr;native_frame_root_relocation::Frame receiver_frame{};};
    std::vector<Pending> pending;
    std::vector<unsigned char> boundaries,seen;
    std::vector<uint32_t> queue;
    std::vector<native_boot_return_continuation::Try> tries;
    std::vector<native_boot_return_continuation::Catch> handlers;
    bool continuation_prepared=false;
    bool PrepareContinuations(size_t frames,size_t units,size_t try_capacity=0,size_t handler_capacity=0){
      if(!units||continuation_prepared)return false;
      pending.reserve(frames);boundaries.resize(units);seen.resize(units);queue.resize(units);
      tries.resize(try_capacity);handlers.resize(handler_capacity);continuation_prepared=true;return true;
    }
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
  Census CensusRecovery(const native_source_stop::Session& owner,void*self,
      const native_cache::Service&threads,Prepared&storage,const modern_field_metadata::ReadContext&read)const {
    Report report;Census census;
    if(!owner.IsStopped(self)||!threads.PreparedForCurrentThread()||!storage.context||!read.owner||!read.extent){census.failure="census-lacks-source-stop";return census;}
    Request request{*this,owner,self,nullptr,storage,report,&read};request.census=&census;request.threads=&threads;
    const bool ok=threads.VisitStoppedWorld(detail::VisitThread,&request,&report.registered_threads,nullptr);
    census.accepted=ok&&report.registered_threads&&report.registered_threads==report.walked_threads&&request.owner_seen&&owner.IsStopped(self);
    census.threads=report.registered_threads;
    census.failure=census.accepted?"":report.failure;census.diagnostic=report;return census;
  }
  // Ordinary phase: exact full census capacity, never a stopped allocation.
  bool PrepareUpcalls(const Census&census,const source_handle_roots::Service&source,Prepared*storage)const {
    if(!storage||!census.accepted||!census.threads||!census.managed_fragments||!storage->upcalls.empty()||
       storage->managed_stack_source!=&source||!source.Calibrated()||!storage->exception_calibration||
       !storage->exception_calibration->MatchesVm(storage->exception_vm)||!storage->upcall_native_owner)return false;
    storage->upcalls.reserve(census.threads);
    for(size_t n=0;n<census.threads;++n){
      auto record=std::unique_ptr<Prepared::Upcall>(new Prepared::Upcall);
      if(!record->chain.Prepare(census.managed_fragments)||!record->boundary.Prepare())return false;
      storage->upcalls.push_back(std::move(record));
    }
    return true;
  }
  bool BindFrameRoots(const native_source_stop::Session&owner,void*self,const native_cache::Service&threads,
      Prepared&storage,const source_commit::Plan&plan,const modern_field_metadata::ReadContext&read,
      native_frame_root_relocation::Interface&proof,FrameRootBinding*out)const noexcept {
    if(!out||storage.recovery_plan!=&plan||!storage.context||storage.frame_roots!=&proof||!owner.IsStopped(self))return false;
    out->service_=this;out->stop_=&owner;out->threads_=&threads;out->self_=self;out->storage_=&storage;
    out->plan_=&plan;out->read_=&read;out->proof_=&proof;return true;
  }
  Report ScanRecovery(const native_source_stop::Session&owner,void*self,
      const native_cache::Service&threads,void*jit,Prepared&storage,
      const source_commit::Plan&plan,const modern_field_metadata::ReadContext&context,
      StoppedRecoveryProof*proof=nullptr)const {
    if(proof)*proof={};Report denied;
    if(storage.recovery_plan!=&plan||!context.owner||!context.extent||!SameShape(storage.recovery_identity,plan)){
      denied.failure="recovery-lacks-prepared-exact-Plan-or-owned-read-context";return denied;
    }
    if(storage.frame_roots&&(!storage.frame_root_resolver||!storage.frame_roots->Begin(owner,self,context,plan,*storage.frame_root_resolver))){denied.failure="whole-frame-root-exact-window-begin-refused";return denied;}
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
  friend class FrameRootBinding;
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
    storage.pending.clear();storage.upcalls_used=0;
    Request request{*this,owner,self,jit,storage,report,context};request.threads=&threads;
    const bool ok=threads.VisitStoppedWorld(detail::VisitThread,&request,&report.registered_threads,nullptr);
    if(ok&&report.registered_threads>0&&report.registered_threads==report.walked_threads&&request.owner_seen&&owner.IsStopped(self)){
      // Native cache enumeration has released thread_list_lock_. Re-prove each
      // registered target and every fragment here; never recurse into that lock.
      for(size_t n=0;n<storage.upcalls_used;++n)if(!storage.upcalls[n]->chain.Validate()){
        report.failure="whole-upcall-registered-fragment-chain-changed";return report;
      }
      if(storage.frame_roots){
        if(!storage.root_provenance){report.failure="whole-frame-original-root-stream-owner-missing";return report;}
        const auto roots=storage.frame_roots->Seal(*storage.root_provenance);
        report.root_failure=static_cast<uint32_t>(roots.failure);report.root_stream_failure=static_cast<uint32_t>(roots.stream.failure);
        report.root_frames=roots.frames;report.root_slots=roots.slots;report.root_callbacks=roots.callbacks;
        if(!roots.completed){report.failure="whole-frame-original-root-stream-seal-refused";return report;}
      }
      if(!AuthorizePending(storage,context,report,owner,self))return report;
      report.accepted=true;report.failure="";}
    else if(!std::strcmp(report.failure,"not-scanned"))report.failure="whole-scan-native-enumeration-or-owner-refused";
    return report;
  }
 private:
  bool AuthorizePending(Prepared&storage,const modern_field_metadata::ReadContext*read,Report&report,const native_source_stop::Session&owner,void*self)const noexcept {
    if(storage.pending.empty())return true;
    if(!storage.frame_roots||!storage.continuation||!read)return (report.failure="whole-continuation-owner-unavailable"),false;
    native_boot_return_continuation::HandlerStorage handlers{storage.tries.data(),storage.tries.size(),storage.handlers.data(),storage.handlers.size()};
    native_boot_return_continuation::Scratch scratch{storage.boundaries.data(),storage.seen.data(),storage.queue.data(),storage.queue.size()};
    auto terminal=[&](void*target){
      if(!target||!storage.recovery_plan||!storage.exception_calibration||
         !storage.exception_calibration->MatchesVm(storage.exception_vm)||!storage.upcall_native_owner||
         !storage.upcall_native_owner->ValidateNativeEntries(owner,self))return false;
      for(size_t n=0;n<storage.upcalls_used;++n){const auto&upcall=*storage.upcalls[n];
        if(upcall.thread==target){
          report.upcall_failure=static_cast<uint32_t>(upcall.boundary.FailureCode());
          report.upcall_fragments=upcall.chain.Fragments();report.upcall_transitions=upcall.boundary.Transitions();
          return upcall.exhausted&&upcall.boundary.Validate(*storage.recovery_plan);
        }
      }return false;
    };
    bool escape=false;void*thread=nullptr;
    for(const auto&p:storage.pending){
      if(thread!=p.frame.thread){if(escape&&!terminal(thread))return (report.failure="whole-continuation-exception-upcall-boundary-unproved"),false;escape=false;thread=p.frame.thread;}
      report.failure_thread=uintptr_t(p.frame.thread);report.failure_method=uintptr_t(p.frame.method);
      report.failure_quick=uintptr_t(p.frame.quick);report.failure_pc=p.frame.pc;report.failure_header=uintptr_t(p.frame.header);
      report.failure_method_prefix=false;report.failure_declaring_class=report.failure_flags=report.failure_dex_index=0;
      report.failure_map_dex_pc=p.dex_pc;report.failure_physical=p.kind==native_callee_edge::Kind::ManagedPhysical;
      if(p.boundary){
       // Fragment transitions and runtime/native callbacks are not the end of
       // WalkStack. Preserve the unresolved obligation through every callback;
       // the actual older managed entry or terminal upcall must discharge it.
       if(escape&&p.kind==native_callee_edge::Kind::Runtime)return (report.failure="whole-continuation-runtime-native-return-owner-unproved"),false;
       if(escape&&p.kind!=native_callee_edge::Kind::Transition)return (report.failure="whole-continuation-exception-noncompiled-owner-unproved"),false;
       continue;
      }
      receiver_guard::CodeView source;native_boot_return_continuation::Shape shape;const char*failure=nullptr;
      if(escape){
        if(!storage.continuation->InspectExceptionalEntry(p.frame.method,p.dex_pc,*read,handlers,scratch,&source,&shape,&failure))return (report.failure=failure?failure:"whole-continuation-older-exception-entry-unproved"),false;
        escape=shape.exception_escape;
      }
      if(!p.aot)continue;
      if(!storage.frame_roots->VerifyLocationRoot(p.receiver_frame,p.receiver_address,p.receiver))return (report.failure="whole-continuation-younger-receiver-root-unproved"),false;
      if(!storage.continuation->InspectContinuation(p.frame.method,p.dex_pc,*read,handlers,scratch,&source,&shape,&failure))return (report.failure=failure?failure:"whole-continuation-current-source-closure-unproved"),false;
      native_interface_dispatch::Match match;
      if(!storage.dispatch||!storage.dispatch->Verify(source,p.dex_pc,p.receiver,p.younger.method,*read,&match,&failure))return (report.failure=failure?failure:"whole-continuation-current-interface-dispatch-unproved"),false;
      escape|=shape.exception_escape;++report.unaffected_aot_frames;
    }
    if(escape&&!terminal(thread))return (report.failure="whole-continuation-exception-upcall-boundary-unproved"),false;
    return true;
  }
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
  struct Request {const Service& service;const native_source_stop::Session& owner;void* self;void* jit;Prepared& storage;Report& report;const modern_field_metadata::ReadContext* recovery;bool owner_seen=false;Census*census=nullptr;bool recapture=false;const native_cache::Service*threads=nullptr;};
  friend bool detail::VisitThread(void*,void*);
  template<class Info,class Pair> struct Visitor final:abi::arm64::stack::Visitor<Pair> {
    unsigned char native_tail[sizeof(abi::arm64::stack::Visitor12)-sizeof(abi::arm64::stack::Visitor<Pair>)+8]{};
    Request& request;void* target;size_t before;
    void** next_quick=nullptr;uintptr_t next_pc=0;bool runtime_successor=false;
    void* observed_method=nullptr;
    Prepared::Upcall*upcall=nullptr;
    native_runtime_frame::FrameInfo upcall_geometry{};
    native_callee_edge::Adjacent adjacent;
    native_callee_edge::Frame previous_fact{};
    const void* previous_receiver_address=nullptr;
    uint32_t previous_receiver_state=0,previous_registers=0,previous_ins=0,
      previous_this=UINT32_MAX,previous_map_first=0,previous_map_count=0;
    int32_t previous_location_kind=-1;uint32_t previous_location_value=0;
    const void*younger_receiver_address=nullptr;native_frame_root_relocation::Frame younger_root_frame{};
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
    bool Boundary(native_callee_edge::Kind kind=native_callee_edge::Kind::Unknown)noexcept {
      auto&r=request;adjacent.Clear();
      previous_fact={target,observed_method,nullptr,this->quick_frame,this->pc,kind};
      previous_receiver_address=nullptr;previous_receiver_state=0;previous_registers=previous_ins=previous_map_first=previous_map_count=0;
      previous_this=UINT32_MAX;previous_location_kind=-1;previous_location_value=0;
      if(r.census){if(r.census->frames==SIZE_MAX||r.census->logical_frames==SIZE_MAX)return Reject("whole-census-capacity-overflow");++r.census->frames;++r.census->logical_frames;}
      if(!r.census&&!r.recapture&&r.storage.continuation){
        if(r.storage.pending.size()==r.storage.pending.capacity())return Reject("whole-continuation-frame-capacity-insufficient");
        Prepared::Pending p;p.frame.thread=target;p.frame.method=observed_method;p.boundary=true;p.kind=kind;r.storage.pending.push_back(p);
      }
      return true;
    }
    bool VisitFrame()override {
      auto& r=request;const auto& s=r.service;
      observed_method=nullptr;
      r.report.failure_map_row=r.report.failure_map_mask=r.report.failure_map_index=UINT32_MAX;
      r.report.failure_map_dex_pc=UINT32_MAX;
      r.report.failure_map_dex_regs=r.report.failure_inline_begin=r.report.failure_inline_end=0;r.report.failure_map_capacity=0;
      if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context)
        return Reject("whole-frame-owner-native-thread-context-changed");
      if(runtime_successor){
       if(this->quick_frame!=next_quick||this->pc!=next_pc)return Reject("whole-runtime-original-walker-successor-differs");
       runtime_successor=false;
      }
      void* method=nullptr;if(!s.stack_.Method(this,sizeof(*this),&method))return Reject("whole-frame-native-method-unavailable");
      observed_method=method;
      if(!method){
       if(upcall&&!upcall->boundary.Transition(uintptr_t(this->quick_frame),this->pc,*r.recovery))upcall->exhausted=false;
       ++r.report.transitions;return Boundary(native_callee_edge::Kind::Transition);}
      ++r.report.frames;
      if(this->shadow_frame&&!this->quick_frame){++r.report.shadow_frames;return Boundary();}
      if(!this->quick_frame||this->shadow_frame)return Reject("whole-frame-unknown-native-cursor");
      // Original GetMethod owns the live ArtMethod. The fixed 11--17 common
      // prefix is Class32, access_flags32; only JNI kAccNative is used here.
      const bool physical=this->inline_frames.begin.row==this->inline_frames.end.row;
      if(physical&&*this->quick_frame!=method)return Reject("whole-frame-original-method-versus-current-quick-frame-mismatch");
      uint32_t flags;std::memcpy(&flags,static_cast<const unsigned char*>(method)+offsetof(abi::arm64::android12::Method12,access_flags),4);
      if(upcall&&physical){
        native_runtime_frame::FrameInfo geometry;
        if(!s.upcall_frame_info_){upcall->boundary.Refuse();}
        else{
         geometry=s.upcall_frame_info_(this);upcall_geometry=geometry;
         if(!r.owner.IsStopped(r.self)||this->quick_frame==nullptr||this->thread!=target||this->context!=r.storage.context||
            !upcall->boundary.Physical(uintptr_t(this->quick_frame),this->pc,geometry.size,geometry.size>=8?geometry.size-8:0,*r.recovery))upcall->boundary.Refuse();
        }
      }

      if(flags&0x100u){if(!physical)return Reject("whole-frame-unproved-native-inlinee");++r.report.native_frames;return Boundary(native_callee_edge::Kind::NativePhysical);}
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
       const auto runtime_begin=this->inline_frames.begin.row,runtime_end=this->inline_frames.end.row;
       if(!s.runtime_.Observe(this,method,this->quick_frame,s.family_==Family::kAndroid11,r.recovery,&info))return Reject("whole-runtime-frame-geometry-or-method-unproved");
       if(upcall&&(upcall_geometry.size!=info.size||upcall_geometry.core_spills!=info.core_spills||upcall_geometry.fp_spills!=info.fp_spills))upcall->boundary.Refuse();
       void* again=nullptr;if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context||this->header||this->shadow_frame||this->inline_frames.begin.row!=runtime_begin||this->inline_frames.end.row!=runtime_end||this->quick_frame!=original_quick||this->pc!=original_pc||!s.stack_.Method(this,sizeof(*this),&again)||again!=method)return Reject("whole-runtime-original-cursor-changed");
       // GetRuntimeMethodName is a fixed named source reader of Runtime-owned
       // callee-save identities. Copy its literal only while the exact original
       // runtime cursor remains stopped; a name never grants helper ownership.
       report.runtime_name_thread=uintptr_t(target);report.runtime_name_method=uintptr_t(method);
       report.runtime_name_quick=uintptr_t(original_quick);report.runtime_name[0]=0;
       report.runtime_name_status=static_cast<uint32_t>(s.runtime_.CopyObservedName(method,r.recovery,report.runtime_name,sizeof(report.runtime_name)));
       again=nullptr;
       if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context||this->header||this->shadow_frame||this->inline_frames.begin.row!=runtime_begin||this->inline_frames.end.row!=runtime_end||this->quick_frame!=original_quick||this->pc!=original_pc||!s.stack_.Method(this,sizeof(*this),&again)||again!=method){
        report.runtime_name[0]=0;report.runtime_name_status=static_cast<uint32_t>(native_runtime_frame::NameStatus::CursorChanged);
        return Reject("whole-runtime-original-cursor-changed");
       }
       const auto base=uintptr_t(original_quick);if(info.size>UINTPTR_MAX-base)return Reject("whole-runtime-frame-address-overflow");
       // WalkStack(include_transitions=true) advances with this exact LR and
       // SP. Even its last quick frame is followed by the fragment transition
       // callback; no guessed PC normalization or missing-successor exemption.
       next_quick=reinterpret_cast<void**>(base+info.size);std::memcpy(&next_pc,reinterpret_cast<void*>(base+info.size-8),8);
       runtime_successor=true;++report.runtime_frames;return Boundary(native_callee_edge::Kind::Runtime);
      }
      if(!r.recovery&&!s.MarkedHeader(this->header))return Reject("whole-frame-compiled-header-unmarked");
      bool unaffected_aot=false;
      if(r.recovery&&!r.census&&!r.recapture){
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
      if(r.recovery&&(unaffected_aot||r.census||r.recapture||r.storage.frame_roots)&&!native_boot_aot_unaffected::TablesMapped(view,*r.recovery))return Reject("whole-AOT-map-tables-lack-current-stop-extent");
      uint32_t pc;
      if(!(physical?view.StackMaps().Read(map.row,2,&pc):view.InlineInfos().Read(this->inline_frames.end.row-1,1,&pc))||pc==UINT32_MAX||s.dex_pc_(this,false)!=pc)return Reject("whole-frame-original-versus-complete-map-DexPc-mismatch");
      if(info->frame_size>UINT32_MAX/16||info->frame_size*16<16||uintptr_t(this->quick_frame)%16)return Reject("whole-physical-frame-geometry-invalid");
      const uint32_t frame_bytes=info->frame_size*16;
      r.report.failure_frame_bytes=frame_bytes;r.report.failure_stack_bit=UINT32_MAX;
      r.report.failure_map_dex_pc=pc;r.report.failure_map_row=map.row;
      r.report.failure_map_dex_regs=view.dex_regs;
      view.StackMaps().Read(map.row,4,&r.report.failure_map_mask);
      view.StackMaps().Read(map.row,3,&r.report.failure_map_index);
      if(upcall&&physical&&(upcall_geometry.size!=frame_bytes||upcall_geometry.core_spills!=info->core_mask||upcall_geometry.fp_spills!=info->fp_mask))upcall->boundary.Refuse();
      receiver_guard::CodeView frame_code{};
      if(r.storage.source_code&&(physical||r.census)&&
         (!r.recovery||!r.storage.source_code->Code(r.self,method,&frame_code,*r.recovery)))
        return Reject("whole-current-source-CodeItem-unreadable");
      compiled_stack_root_window::Window incoming;
      if(physical&&r.storage.source_code&&
         !compiled_stack_root_window::Window::BindSameMethodCode(method,this->quick_frame,frame_bytes,frame_code,*r.recovery,&incoming))
        return Reject("whole-current-incoming-parameter-source-unproved");
      if(physical&&r.census){
        size_t slots=0;uint32_t stack_index=0,register_index=0;
        if(!view.StackMaps().Read(map.row,4,&stack_index)||!view.StackMaps().Read(map.row,3,&register_index))return Reject("whole-census-mask-unreadable");
        if(stack_index!=UINT32_MAX)for(size_t bit=0;bit<view.tables[2].row_bits;++bit){bool set=false;
          if(bit>UINT32_MAX||!decoded_frame_recovery::MaskBit(view.tables[2],stack_index,uint32_t(bit),&set))return Reject("whole-census-stackmask-unreadable");
          if(set){const void*address=nullptr;size_t extent=0;
            if(!incoming.Slot(method,this->quick_frame,frame_bytes,uint32_t(bit),&address)){
              r.report.failure_stack_bit=uint32_t(bit);return Reject("whole-census-stackmask-outside-owned-frame-or-parameters");}
            if(!r.recovery->extent(r.recovery->owner,address,&extent)||extent<4){
              r.report.failure_stack_bit=uint32_t(bit);return Reject("whole-census-stackmask-slot-unmapped");}
            ++slots;}}
        if(register_index!=UINT32_MAX){uint32_t value=0,shift=0;
          if(!view.tables[1].Read(register_index,0,&value)||!view.tables[1].Read(register_index,1,&shift)||shift>=32||value>UINT32_MAX>>shift)return Reject("whole-census-registermask-unreadable");
          value<<=shift;for(unsigned reg=0;reg<32;++reg)slots+=(value>>reg)&1u;}
        if(r.census->frames==SIZE_MAX||slots>SIZE_MAX-r.census->slots)return Reject("whole-census-capacity-overflow");
        ++r.census->frames;r.census->slots+=slots;
      }
      if(r.census){
        if(r.census->logical_frames==SIZE_MAX)return Reject("whole-census-capacity-overflow");
        ++r.census->logical_frames;
        if(r.storage.source_code){
          const auto capacity=native_boot_return_continuation::CountStorage(frame_code,*r.recovery);
          if(!capacity.complete)return Reject("whole-census-source-complete-handler-list-unreadable");
          r.census->code_units=std::max(r.census->code_units,capacity.units);
          r.census->tries=std::max(r.census->tries,capacity.tries);
          r.census->handlers=std::max(r.census->handlers,capacity.handlers);
        }
      }
      native_frame_root_relocation::Frame root_frame{target,0,method,this->quick_frame,this->header,this->pc,r.storage.context,frame_bytes,s.family_,incoming};
      if(physical&&!r.census&&r.storage.frame_roots&&!r.storage.frame_roots->CaptureFrame(root_frame,view,map.row))return Reject("whole-frame-root-capture-refused");
      if(r.census||r.recapture){++r.report.compiled_frames;return true;}
      r.report.failure_map_dex_pc=pc;
      r.report.failure_map_row=map.row;r.report.failure_map_dex_regs=view.dex_regs;
      r.report.failure_inline_begin=this->inline_frames.begin.row;r.report.failure_inline_end=this->inline_frames.end.row;r.report.failure_map_capacity=r.storage.locations.size();
      view.StackMaps().Read(map.row,6,&r.report.failure_map_mask);view.StackMaps().Read(map.row,7,&r.report.failure_map_index);
      if(!decoded_frame_recovery::Decode(view,map.row,this->inline_frames.begin.row,this->inline_frames.end.row,r.storage.locations.data(),r.storage.locations.size(),&range))return Reject("whole-frame-incomplete-dex-register-map");
      auto& native=r.storage.native_map;
      // The source decoder fills only Invalid entries. Both the small buffer
      // and reused large buffer must be reset for this exact current frame.
      if(!native.Reset(range.count))return Reject("whole-frame-native-map-scratch-capacity-insufficient");
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
      native_callee_edge::Frame fact{target,method,nullptr,this->quick_frame,this->pc,
        physical?native_callee_edge::Kind::ManagedPhysical:native_callee_edge::Kind::Inline};
      native_callee_edge::Frame younger{};const bool has_younger=adjacent.Younger(fact,&younger);
      auto& diagnostic=r.report;
      diagnostic.receiver_caller_thread=uintptr_t(target);diagnostic.receiver_caller_method=uintptr_t(method);
      diagnostic.receiver_caller_quick=uintptr_t(this->quick_frame);diagnostic.receiver_caller_pc=this->pc;
      diagnostic.younger_present=has_younger;
      diagnostic.previous_kind=static_cast<uint32_t>(previous_fact.kind);
      diagnostic.younger_method=uintptr_t(previous_fact.method);diagnostic.younger_quick=uintptr_t(previous_fact.quick);diagnostic.younger_pc=previous_fact.pc;
      diagnostic.receiver_state=previous_receiver_state;diagnostic.receiver_registers=previous_registers;diagnostic.receiver_ins=previous_ins;
      diagnostic.receiver_this_index=previous_this;diagnostic.receiver_map_first=previous_map_first;diagnostic.receiver_map_count=previous_map_count;
      diagnostic.receiver_location_kind=previous_location_kind;diagnostic.receiver_location_value=previous_location_value;
      diagnostic.receiver_slot=uintptr_t(previous_receiver_address);diagnostic.receiver_value=uintptr_t(previous_fact.receiver);
      const void* receiver_address=nullptr;
      uint32_t receiver_state=0,receiver_registers=0,receiver_ins=0,receiver_this=UINT32_MAX;
      int32_t receiver_kind=-1;uint32_t receiver_location=0;
      // States: 0 uninspected, 1 static, 2 source unavailable, 3 invalid ins,
      // 4 mismatched map, 5 dead, 6 unsupported, 7 null stack, 8 null GPR,
      // 9 live stack, 10 live GPR. Preserve the original strict acceptance.
      if(physical&&r.storage.continuation){receiver_guard::CodeView callee_code;
       if(flags&0x8u)receiver_state=1;
       else if(!r.storage.continuation->CurrentCalleeCode(method,*r.recovery,&callee_code))receiver_state=2;
       else {receiver_registers=callee_code.registers;receiver_ins=callee_code.ins_size;
        if(!callee_code.ins_size||callee_code.ins_size>callee_code.registers)receiver_state=3;
        else if(range.first!=0||range.count!=callee_code.registers)receiver_state=4;
        else {receiver_this=callee_code.registers-callee_code.ins_size;
         const auto location=r.storage.locations[receiver_this];receiver_kind=location.kind;receiver_location=location.value;
         if(location.kind==0){receiver_address=reinterpret_cast<const unsigned char*>(this->quick_frame)+location.value;uint32_t bits=0;std::memcpy(&bits,receiver_address,4);fact.receiver=reinterpret_cast<void*>(uintptr_t(bits));receiver_state=bits?9:7;}
         else if(location.kind==2&&location.value<32&&r.storage.context->IsAccessibleGPR(location.value)){receiver_address=r.storage.context->GetGPRAddress(location.value);fact.receiver=reinterpret_cast<void*>(r.storage.context->GetGPR(location.value));receiver_state=fact.receiver?10:8;}
         else receiver_state=location.kind==-1?5:6;
        }
       }
      }
      void* current=nullptr;
      if(!r.owner.IsStopped(r.self)||this->thread!=target||this->context!=r.storage.context||
         this->header!=original_header||(!r.recovery&&!s.MarkedHeader(original_header))||this->quick_frame!=original_quick||this->pc!=original_pc||
         this->shadow_frame||this->inline_frames.begin.row!=original_begin||this->inline_frames.end.row!=original_end||
         !s.stack_.Method(this,sizeof(*this),&current)||current!=method||s.dex_pc_(this,false)!=pc)return Reject("whole-frame-source-owner-or-native-cursor-lost-after-map");
      bool deferred_aot=false;
      if(unaffected_aot){
       if(s.async_.Query(*this->quick_frame,this->pc,*r.recovery)||s.MarkedHeader(this->header)||
          s.code_owner_.Observe(*this->quick_frame,this->pc,this->header,r.jit)!=native_frame_code_owner::Kind::Aot)return Reject("whole-AOT-current-code-owner-or-async-state-changed");
       const char*failure=nullptr;
       if(!r.storage.boot_unaffected->Verify(method,pc,view,map.row,range,*r.recovery,&failure)){
        // Preserve the first Source rejection even if a later continuation
        // owner reports a different boundary. Diagnostic literals grant nothing.
        r.report.aot_verify_failure=failure;r.report.aot_verify_thread=uintptr_t(target);
        r.report.aot_verify_method=uintptr_t(method);r.report.aot_verify_dex_pc=pc;
        if(!r.storage.continuation||!r.storage.frame_roots||!r.storage.root_provenance||!r.storage.dispatch)return Reject(failure?failure:"whole-AOT-current-body-prefix-map-or-dependency-closure-refused");
        if(!physical||!has_younger||!younger.receiver||!younger_receiver_address)return Reject("whole-continuation-actual-younger-receiver-unproved");
        if(r.storage.pending.size()==r.storage.pending.capacity())return Reject("whole-continuation-frame-capacity-insufficient");
        deferred_aot=true;
       }else ++r.report.unaffected_aot_frames;
      }else if(r.recovery&&!s.async_.Query(*this->quick_frame,this->pc,*r.recovery))return Reject("whole-frame-original-async-recovery-changed-after-map");
      if(r.storage.continuation){
       if(r.storage.pending.size()==r.storage.pending.capacity())return Reject("whole-continuation-frame-capacity-insufficient");
       r.storage.pending.push_back({root_frame,pc,deferred_aot,false,fact.kind,younger,younger_receiver_address,younger.receiver,younger_root_frame});
       // Every logical frame participates in exceptional unwinding. An inline
       // callback must not manufacture a physical entered-callee edge.
       if(physical){adjacent.Observe(fact);younger_receiver_address=receiver_address;younger_root_frame=root_frame;}
       else adjacent.Clear();
      }else adjacent.Clear();
      previous_fact=fact;previous_receiver_address=receiver_address;previous_receiver_state=receiver_state;previous_registers=receiver_registers;previous_ins=receiver_ins;previous_this=receiver_this;
      previous_map_first=range.first;previous_map_count=range.count;previous_location_kind=receiver_kind;previous_location_value=receiver_location;
      ++r.report.compiled_frames;if(!physical)++r.report.inlined_frames;r.report.map_locations+=range.count;return true;
    }
  };
  template<class Info,class Pair> bool Walk(Request& r,void* target)const {
    r.storage.context->Reset();Visitor<Info,Pair> visitor(r,target);
    if(r.storage.managed_stack_source&&r.recovery&&!r.recapture){
      source_handle_roots::ManagedStackSnapshot head;
      if(!r.storage.managed_stack_source->ReadEnumeratedStoppedManagedStack(r.owner,r.self,target,*r.recovery,&head)){
        r.report.failure="whole-upcall-enumerated-thread-managed-head-unproved";return false;
      }
      if(r.census){
        const auto fragments=managed_stack_source::Count(*r.recovery,head.address);
        if(!fragments.complete){r.report.failure="whole-census-managed-fragment-chain-invalid";return false;}
        r.census->managed_fragments=std::max(r.census->managed_fragments,fragments.fragments);
      }else{
        if(!r.threads||r.storage.upcalls_used==r.storage.upcalls.size()){
          r.report.failure="whole-upcall-thread-capacity-insufficient";return false;
        }
        auto&upcall=*r.storage.upcalls[r.storage.upcalls_used++];upcall.thread=target;upcall.exhausted=false;
        if(!upcall.chain.CaptureEnumerated(*r.storage.managed_stack_source,r.owner,r.self,target,*r.threads,*r.recovery,head).complete){
          r.report.failure="whole-upcall-complete-fragment-capture-refused";return false;
        }
        upcall.boundary.Begin(upcall.chain,*r.storage.recovery_plan);visitor.upcall=&upcall;
      }
    }

    if(!stack_.Construct(&visitor,sizeof(visitor),target,r.storage.context,abi::arm64::stack::WalkKind::kIncludeInlined)||
       visitor.thread!=target||visitor.context!=r.storage.context||visitor.kind!=abi::arm64::stack::WalkKind::kIncludeInlined){r.report.failure="whole-thread-original-constructor-geometry-mismatch";return false;}
    if(!stack_.Walk(&visitor,sizeof(visitor),true)){r.report.failure="whole-thread-native-walk-unavailable";return false;}
    if(visitor.upcall){
      visitor.upcall->exhausted=visitor.upcall->boundary.ExhaustedNormally();
      ++r.report.upcall_threads;r.report.upcall_closed+=visitor.upcall->exhausted;
    }
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
  native_runtime_frame::FrameInfo(*upcall_frame_info_)(const void*)=nullptr;
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

inline bool artdcevm::whole_process_frames::FrameRootBinding::Recapture(bool after)const noexcept {
 if(!service_||!stop_||!threads_||!storage_||!plan_||!read_||!proof_||!stop_->IsStopped(self_))return false;
 auto&storage=*static_cast<Service::Prepared*>(storage_);
 if(storage.recovery_plan!=plan_||storage.frame_roots!=proof_||!source_commit::SameIdentity(storage.recovery_identity,*plan_)||!proof_->BeginRecapture(*plan_,after))return false;
 Report report;Service::Request request{*service_,*stop_,self_,nullptr,storage,report,read_};request.recapture=true;
 const bool ok=threads_->VisitStoppedWorld(detail::VisitThread,&request,&report.registered_threads,nullptr);
 return ok&&report.registered_threads&&report.registered_threads==report.walked_threads&&request.owner_seen&&stop_->IsStopped(self_)&&proof_->EndRecapture();
}

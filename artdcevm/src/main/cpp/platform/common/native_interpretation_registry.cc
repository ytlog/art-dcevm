#include "platform/common/native_interpretation_registry.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <new>
#include <atomic>
#include <cstring>
#include "platform/common/runtime_service_calibration.h"
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/readable_mapping_scope.h"
#include "platform/common/native_source_compilation.h"

namespace artdcevm::native_interpretation_registry {
namespace {
bool Less(void*a,void*b)noexcept{return reinterpret_cast<uintptr_t>(a)<reinterpret_cast<uintptr_t>(b);}
bool Has(const std::vector<void*>&methods,void*method)noexcept{
 return method&&std::binary_search(methods.begin(),methods.end(),method,Less);
}
constexpr char kQuery[]="_ZN3art15instrumentation15Instrumentation13IsDeoptimizedEPNS_9ArtMethodE";
constexpr char kCodeQuery[]="_ZN3art15instrumentation15Instrumentation16GetCodeForInvokeEPNS_9ArtMethodE";
constexpr char kIsBridge[]="_ZNK3art11ClassLinker26IsQuickToInterpreterBridgeEPKv";
constexpr char kReinitialize[]="_ZN3art15instrumentation15Instrumentation23ReinitializeMethodsCodeEPNS_9ArtMethodE";
constexpr std::array<const char*,7>kEntryUpdates{{
 "_ZN3art15instrumentation15Instrumentation30UpdateEntrypointsForDebuggableEv",
 "_ZN3art15instrumentation15Instrumentation21DisableDeoptimizationEPKcb",
 "_ZN3art15instrumentation15Instrumentation20EnableEntryExitHooksEPKc",
 "_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc",
 "_ZN3art15instrumentation15Instrumentation22UndeoptimizeEverythingEPKc",
 "_ZN3art15instrumentation15Instrumentation19EnableMethodTracingEPKcPNS0_23InstrumentationListenerEb",
 "_ZN3art15instrumentation15Instrumentation20DisableMethodTracingEPKc"}};
std::atomic<Registry*>query_owner{nullptr};
thread_local Registry*probe_owner=nullptr;
thread_local void*probe_method=nullptr;
thread_local size_t probe_calls=0;
thread_local size_t compilation_probe_calls=0;
thread_local size_t code_probe_calls=0;
class ProbeScope final {
 public:
 ProbeScope(Registry&owner,void*method){
  if(probe_owner||!method)std::abort();
  probe_owner=&owner;probe_method=method;
  probe_calls=0;compilation_probe_calls=0;code_probe_calls=0;
 }
 ~ProbeScope(){probe_method=nullptr;probe_owner=nullptr;}
 ProbeScope(const ProbeScope&)=delete;ProbeScope&operator=(const ProbeScope&)=delete;
};
using namespace abi::arm64::android_common::method_compilation_policy;
static_assert(offsetof(class_metadata::Class11,methods)==offsetof(class_metadata::Class16,methods)&&
              offsetof(class_metadata::Class16,methods)==offsetof(class_metadata::Class17,methods));
struct Methods {
 void*array=nullptr;uint32_t count=0;size_t stride=0;
 void*At(uint32_t i)const noexcept{return static_cast<unsigned char*>(array)+8+size_t(i)*stride;}
};
// The hooked original Class member supplies a live Class and its ART-owned
// length-prefixed array under mutator access. This view is only for that call;
// it does not certify DEX declarations, roots or transaction completeness.
bool ClassMethods(void*klass,size_t stride,Methods*out)noexcept{
 if(!klass||!out||!stride)return false;
 uint64_t raw=0;std::memcpy(&raw,static_cast<unsigned char*>(klass)+offsetof(class_metadata::Class11,methods),8);
 void*array=reinterpret_cast<void*>(uintptr_t(raw));uint32_t count=0;
 if(array){const auto address=uintptr_t(array);
  if(address%8||address>UINTPTR_MAX-8)return false;
  std::memcpy(&count,array,4);
  if(count>(UINTPTR_MAX-address-8)/stride)return false;
 }
 *out={array,count,stride};return true;
}
uint32_t*Flags(void*method)noexcept{return reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4);}
bool Protected(const Registry&owner,void*method)noexcept{
 return (probe_owner==&owner&&probe_method==method)||owner.Contains(method);
}
bool SameLayout(const method_metadata::Layout&a,const method_metadata::Layout&b)noexcept{
 return a.size==b.size&&a.dex_method_index==b.dex_method_index&&a.data==b.data&&a.entry_point==b.entry_point;
}
}
struct QueryHooks {
 static bool Invoke(void*instrumentation,void*method){
  auto*owner=query_owner.load(std::memory_order_acquire);
  if(!owner||!owner->original_query_)std::abort();
  if(instrumentation==owner->instrumentation_&&owner->runtime_slot_&&
     *owner->runtime_slot_==owner->runtime_&&method){
   if(probe_owner==owner&&probe_method==method){++probe_calls;return true;}
   // No ArtMethod/Class access: allocator cleanup can safely erase opaque
   // addresses before destruction, without any Instrumentation set mutation.
   if(owner->Contains(method))return true;
  }
  return owner->original_query_(instrumentation,method);
 }
};
struct CompilationHooks {
 static void ClearClass(void*klass,PointerSize pointer_size){
  auto*owner=query_owner.load(std::memory_order_acquire);
  if(!owner||!owner->original_clear_class_)std::abort();
  if(pointer_size!=PointerSize::k64||!owner->runtime_slot_||*owner->runtime_slot_!=owner->runtime_){
   owner->original_clear_class_(klass,pointer_size);return;
  }
  Methods methods{};if(!ClassMethods(klass,owner->method_layout_.size,&methods))std::abort();
  bool protected_member=false;
  for(uint32_t i=0;i<methods.count;++i)protected_member|=Protected(*owner,methods.At(i));
  if(!protected_member){owner->original_clear_class_(klass,pointer_size);return;}
  // Exact original predicate and atomic ClearAccessFlags, with the owned
  // methods excluded before any write. Clearing then restoring the bit would
  // allow a concurrent compiler to observe a temporarily compilable method.
  for(uint32_t i=0;i<methods.count;++i){void*method=methods.At(i);
   if(method_metadata::ReadAccessFlags(method)&kNotManagedInvokable)continue;
   if(Protected(*owner,method)){
    if(probe_owner==owner&&probe_method==method)++compilation_probe_calls;
    continue;
   }
   __atomic_fetch_and(Flags(method),~kDontCompile,__ATOMIC_RELAXED);
  }
 }
};
struct EntryQueryHooks {
 static const void*Invoke(void*instrumentation,void*method){
  auto*owner=query_owner.load(std::memory_order_acquire);
  if(!owner||!owner->original_code_)std::abort();
  if(instrumentation==owner->instrumentation_&&owner->runtime_slot_&&*owner->runtime_slot_==owner->runtime_&&
     method&&Protected(*owner,method)){
   // Obsolete carriers retain ART's InvokeObsoleteMethodStub and its alias /
   // access handling. Returning the ordinary interpreter bridge for them
   // would revive the old body through a retained method reference.
   if(method_metadata::ReadAccessFlags(method)&0x40000u)
    return owner->original_code_(instrumentation,method);
   if(!owner->interpreter_bridge_)std::abort();
   if(probe_owner==owner&&probe_method==method)++code_probe_calls;
   return owner->interpreter_bridge_;
  }
  return owner->original_code_(instrumentation,method);
 }
};
struct EntryUpdateHooks {
 static Registry&Owner(){auto*owner=query_owner.load(std::memory_order_acquire);if(!owner)std::abort();return *owner;}
 struct Exit {
  Registry&owner;void*instrumentation;
  ~Exit(){owner.RestoreProtectedEntries(instrumentation);}
 };
 static void Debuggable(void*instrumentation){auto&owner=Owner();
  Exit exit{owner,instrumentation};
  reinterpret_cast<Registry::EntryUpdate>(owner.update_originals_[0])(instrumentation);
 }
 static void Disable(void*instrumentation,const char*key,bool switch_runtime){auto&owner=Owner();
  Exit exit{owner,instrumentation};
  reinterpret_cast<Registry::DisableUpdate>(owner.update_originals_[1])(instrumentation,key,switch_runtime);
 }
 template<size_t Index>static void Key(void*instrumentation,const char*key){auto&owner=Owner();
  Exit exit{owner,instrumentation};
  reinterpret_cast<Registry::KeyUpdate>(owner.update_originals_[Index])(instrumentation,key);
 }
 static void Trace(void*instrumentation,const char*key,void*listener,bool interpreter){auto&owner=Owner();
  Exit exit{owner,instrumentation};
  reinterpret_cast<Registry::TraceUpdate>(owner.update_originals_[5])(instrumentation,key,listener,interpreter);
 }
};
Registry::~Registry(){if(pending_||query_token_||compilation_token_||code_token_||
 std::any_of(update_tokens_.begin(),update_tokens_.end(),[](void*token){return token!=nullptr;})||
 retirement::HasInterpretationCleanup(*this))std::abort();}
Registry::Reservation::~Reservation(){if(!committed_)owner_.Cancel(*this);}
bool Registry::Initialize(HookEngine&hooks,const runnable_jni::Context&context,
 const method_identity::Calibration&calibration,const modern_field_metadata::FieldMetadata&metadata,
 const bounded_managed_roots::Store&roots,const native_selective_interpretation::Method&yes,
 const native_selective_interpretation::Method&no,const runtime_services::Snapshot&runtime,
 native_source_compilation::Service&compilation,const native_source_stop::Service&source,
 size_t self_extent,const retirement::Owners&retired,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!context.env||!context.vm||!context.self||!self_extent||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    context.env->ExceptionCheck()||!native_source_stop::Session::InspectionAvailable()||!source.Calibrated()||
    !yes.method||!no.method||yes.method==no.method||query_token_||pending_||Count()||
    !runtime_||!runtime_slot_||*runtime_slot_!=runtime_||!instrumentation_||!class_linker_)
  return deny("interpretation-bootstrap-needs-original-runnable-scope-and-fresh-rooted-fixture");
 native_selective_interpretation::Service positive,negative;
 retirement::CleanupBinding cleanup;
 // BindQuery independently calibrated these receivers at the ordinary
 // Native boundary. Re-entering that producer here would nest its Runnable
 // executor inside this retained original callback and cannot prove ownership.
 // Registry is the sole private friend supplying this already observed owner;
 // no caller-selected Snapshot or readiness Boolean binds either journal.
 positive.runtime_=negative.runtime_=runtime_;
 positive.instrumentation_=negative.instrumentation_=instrumentation_;
 positive.runtime_slot_=negative.runtime_slot_=runtime_slot_;
 // All remaining preparation precedes the fresh mapping snapshot and stop.
 // The journals preserve ART's native set independently of persistent policy.
 if(!BindCompilationPolicy(hooks,calibration,metadata,error)||
    !BindEntryQuery(hooks,error)||!BindEntryUpdates(hooks,error)||
    !positive.Prepare(hooks,calibration,metadata,roots,&yes,1,error)||!positive.PrepareRefund(hooks,error)||
    !negative.Prepare(hooks,calibration,metadata,roots,&no,1,error))return false;
 if(runtime.runtime!=runtime_||runtime.instrumentation!=instrumentation_||runtime.receivers.class_linker!=class_linker_)
  return deny("interpretation-bootstrap-compilation-runtime-owner-differs-from-independent-observation");
 if(!compilation.Bind(hooks,runtime,error)||!cleanup.Bind(hooks,error))return false;
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||context.env->ExceptionCheck())
  return deny("interpretation-bootstrap-original-context-lost-after-preparation");
 method_identity::ScopedReadableMappings mappings;
 if(!mappings.Ready())return deny("interpretation-bootstrap-fresh-mappings-unavailable");
 const modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 struct Request {
  Registry&registry;HookEngine&hooks;void*self;
  native_source_compilation::Service&compilation;
  const retirement::CleanupBinding&cleanup;const retirement::Owners&retired;
  const modern_field_metadata::FieldMetadata&metadata;
  native_selective_interpretation::Service&positive;const native_selective_interpretation::Service&negative;
  const modern_field_metadata::ReadContext&read;
  std::string error;bool completed=false;
 }request{*this,hooks,context.self,compilation,cleanup,retired,metadata,positive,negative,read,{},false};
 native_source_stop::Session stop(source);
 const bool inspected=stop.Inspect(context.self,self_extent,&request,[](void*raw,native_source_stop::Session&actual)noexcept{
  auto&r=*static_cast<Request*>(raw);
  try{
   const char*failure=nullptr;
   if(!r.registry.InstallQuery(r.hooks,actual,r.self,&r.error))return false;
   if(!r.registry.CalibrateQuery(actual,r.self,r.positive,r.negative,r.read,&failure)){
    r.error=failure?failure:"interpretation-bootstrap-query-probe-refused";return false;
   }
   if(!r.registry.InstallCompilationPolicy(r.hooks,actual,r.self,&r.error))return false;
   if(!r.registry.CalibrateCompilationPolicy(actual,r.self,r.metadata,r.positive,r.negative,r.read,&failure)){
    r.error=failure?failure:"interpretation-bootstrap-compilation-probe-refused";return false;
   }
   if(!r.registry.CalibrateEntryQuery(r.hooks,actual,r.self,r.positive,r.negative,r.read,&r.error)||
      !r.registry.InstallEntryUpdates(r.hooks,actual,r.self,&r.error)||
      !r.compilation.Install(r.hooks,actual,r.self,&r.error)||
      !r.cleanup.InstallStopped(r.hooks,actual,r.self,r.retired.membership,r.retired.fields,r.retired.methods,
        &r.registry,&r.error))return false;
   // CalibrateEntryQuery owns and refunds its temporary native entry. No
   // fixture key is published to the durable registry by initialization.
   r.completed=actual.IsStopped(r.self)&&!r.positive.PendingRefund()&&!r.registry.Count()&&
     retirement::HasInterpretationCleanup(r.registry);
   return r.completed;
  }catch(const std::bad_alloc&){return false;}catch(...){return false;}
 });
 if(stop.Reserved()||stop.IsStopped(context.self)||positive.PendingRefund())std::abort();
 if(!inspected||!request.completed)return deny(request.error.empty()?
   "interpretation-bootstrap-actual-inspection-or-calibration-refused":request.error.c_str());
 return runnable_jni::Bridge::BorrowOriginalScope(context)&&!context.env->ExceptionCheck()?
  true:deny("interpretation-bootstrap-original-runnable-context-not-restored");
}
bool Registry::BindQuery(HookEngine&hooks,JNIEnv*env,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(runtime_)return runtime_slot_&&*runtime_slot_==runtime_&&instrumentation_&&query_target_?
  true:deny("interpretation-query-runtime-owner-changed");
 runtime_services::Snapshot observed{};
 if(!runtime_services::CalibrateRuntimeServices(hooks,env,&observed,error))return false;
 auto slot=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 auto target=reinterpret_cast<Query>(hooks.FindSymbol("libart.so",kQuery));
 if(!slot||*slot!=observed.runtime||!observed.runtime||!observed.instrumentation||!observed.receivers.class_linker||!target)
  return deny("interpretation-query-original-runtime-owner-unavailable");
 runtime_=observed.runtime;runtime_slot_=slot;instrumentation_=observed.instrumentation;
 class_linker_=observed.receivers.class_linker;query_target_=target;return true;
}
bool Registry::InstallQuery(HookEngine&hooks,const native_source_stop::Session&stop,void*self,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!stop.IsStopped(self)||!runtime_slot_||*runtime_slot_!=runtime_||!instrumentation_||!query_target_)
  return deny("interpretation-query-installation-needs-actual-stop-and-runtime");
 if(query_token_)return query_owner.load(std::memory_order_acquire)==this||deny("interpretation-query-installed-owner-changed");
 Registry*expected=nullptr;
 if(!query_owner.compare_exchange_strong(expected,this,std::memory_order_acq_rel))
  return deny("interpretation-query-already-has-process-owner");
 struct Publication {
  Registry*owner;
  ~Publication(){if(!owner->query_token_){
   if(owner->original_query_)std::abort();
   query_owner.store(nullptr,std::memory_order_release);
  }}
 } publication{this};
 query_token_=hooks.Install(reinterpret_cast<void*>(query_target_),reinterpret_cast<void*>(QueryHooks::Invoke),
  reinterpret_cast<void**>(&original_query_),error);
 if(!query_token_){original_query_=nullptr;return false;}
 if(!original_query_||original_query_==query_target_||!stop.IsStopped(self))std::abort();
 query_owner.store(this,std::memory_order_release);
 return true;
}
bool Registry::ValidateProbePair(const native_source_stop::Session&stop,void*self,
 const native_selective_interpretation::Service&positive,const native_selective_interpretation::Service&negative,
 const modern_field_metadata::ReadContext&read,RootedProbes*out,const char**failure)const noexcept{
 const auto deny=[&](const char*why){if(failure)*failure=why;return false;};
 if(failure)*failure=nullptr;if(out)*out={};
 if(!out||!query_token_||query_owner.load(std::memory_order_acquire)!=this||!original_query_||
    !runtime_slot_||*runtime_slot_!=runtime_||!stop.IsStopped(self)||probe_owner||
    !positive.PreparedSelection()||!negative.PreparedSelection()||
    positive.SelectedMethods().empty()||negative.SelectedMethods().empty()||
    positive.runtime_!=runtime_||negative.runtime_!=runtime_||
    positive.instrumentation_!=instrumentation_||negative.instrumentation_!=instrumentation_||
    positive.runtime_slot_!=runtime_slot_||negative.runtime_slot_!=runtime_slot_||
    !SameLayout(positive.MethodLayout(),negative.MethodLayout())||
    (method_layout_.size&&!SameLayout(positive.MethodLayout(),method_layout_)))
  return deny("interpretation-independent-rooted-probe-runtime-or-layout-unproved");
 if(!positive.ValidateIdentities(stop,self,read,failure)||!negative.ValidateIdentities(stop,self,read,failure))return false;
 void*yes=positive.SelectedMethods().front().method;void*no=negative.SelectedMethods().front().method;
 if(yes==no||Contains(yes)||Contains(no)||original_query_(instrumentation_,yes)||original_query_(instrumentation_,no))
  return deny("interpretation-probes-are-not-distinct-original-negatives");
 *out={yes,no};return true;
}
bool Registry::CalibrateQuery(const native_source_stop::Session&stop,void*self,
 const native_selective_interpretation::Service&positive,const native_selective_interpretation::Service&negative,
 const modern_field_metadata::ReadContext&read,const char**failure)noexcept{
 const auto deny=[&](const char*why){if(failure)*failure=why;return false;};if(failure)*failure=nullptr;
 if(!query_target_)return deny("interpretation-query-original-target-unavailable");
 RootedProbes pair{};if(!ValidateProbePair(stop,self,positive,negative,read,&pair,failure))return false;
 void*yes=pair.positive;void*no=pair.negative;
 ProbeScope probe(*this,yes);
 const bool accepted=query_target_(instrumentation_,yes);
 const size_t hits=probe_calls;
 const bool rejected=!query_target_(instrumentation_,no);
 if(!accepted||!rejected||hits!=1||probe_calls!=hits||!stop.IsStopped(self)||*runtime_slot_!=runtime_||
    original_query_(instrumentation_,yes)||original_query_(instrumentation_,no))
  return deny("interpretation-query-original-positive-negative-forwarding-unproved");
 query_calibrated_=true;return true;
}
bool Registry::BindCompilationPolicy(HookEngine&hooks,const method_identity::Calibration&calibration,
 const modern_field_metadata::FieldMetadata&metadata,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!runtime_slot_||*runtime_slot_!=runtime_||!metadata.Calibrated()||calibration.identities.size()<3||
    calibration.identities.size()!=calibration.readable_extents.size())return deny("interpretation-compilation-source-calibration-unavailable");
 if(calibration.model!=method_identity::Model::kMethod11&&calibration.model!=method_identity::Model::kMethod12Onward)
  return deny("interpretation-compilation-method-model-unproved");
 const auto&layout=calibration.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 if(clear_class_target_)return class_model_==metadata.StorageModel()&&SameLayout(method_layout_,layout)&&clear_intrinsic_?
  true:deny("interpretation-compilation-source-owner-changed");
 auto clear=reinterpret_cast<abi::arm64::android_common::method_compilation_policy::ClearClass>(hooks.FindSymbol("libart.so",kClearClass));
 auto intrinsic=reinterpret_cast<ClearIntrinsic>(hooks.FindSymbol("libart.so",kClearIntrinsic));
 if(!clear||!intrinsic)return deny("interpretation-compilation-named-class-or-intrinsic-operation-unavailable");
 class_model_=metadata.StorageModel();method_layout_=layout;clear_class_target_=clear;clear_intrinsic_=intrinsic;return true;
}
bool Registry::InstallCompilationPolicy(HookEngine&hooks,const native_source_stop::Session&stop,void*self,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!stop.IsStopped(self)||!query_token_||query_owner.load(std::memory_order_acquire)!=this||
    !runtime_slot_||*runtime_slot_!=runtime_||!method_layout_.size||!clear_class_target_||!clear_intrinsic_)
  return deny("interpretation-compilation-installation-owner-or-stop-unavailable");
 if(compilation_token_)return true;
 compilation_token_=hooks.Install(reinterpret_cast<void*>(clear_class_target_),reinterpret_cast<void*>(CompilationHooks::ClearClass),
  reinterpret_cast<void**>(&original_clear_class_),error);
 if(!compilation_token_){original_clear_class_=nullptr;return false;}
 if(!original_clear_class_||original_clear_class_==clear_class_target_||!stop.IsStopped(self))std::abort();
 query_owner.store(this,std::memory_order_release);return true;
}
bool Registry::CalibrateCompilationPolicy(const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::FieldMetadata&metadata,
 const native_selective_interpretation::Service&positive,const native_selective_interpretation::Service&negative,
 const modern_field_metadata::ReadContext&read,const char**failure)noexcept{
 const auto deny=[&](const char*why){if(failure)*failure=why;return false;};if(failure)*failure=nullptr;
 if(!compilation_token_||!query_calibrated_||
    !metadata.Calibrated()||metadata.StorageModel()!=class_model_||
    !method_layout_.size||!read.owner||!read.extent)
  return deny("interpretation-compilation-independent-rooted-probes-unavailable");
 RootedProbes pair{};if(!ValidateProbePair(stop,self,positive,negative,read,&pair,failure))return false;
 void*yes=pair.positive;void*no=pair.negative;
 void*klass=metadata.MethodDeclaringClass(yes);size_t extent=0;
 if(!klass||metadata.MethodDeclaringClass(no)!=klass||
    !read.extent(read.owner,klass,&extent)||extent<offsetof(class_metadata::Class11,methods)+8)
  return deny("interpretation-compilation-probes-require-one-distinct-rooted-fixture-class");
 Methods methods{};
 if(!ClassMethods(klass,method_layout_.size,&methods)||!methods.array||!read.extent(read.owner,methods.array,&extent)||
    extent<8||methods.count>(extent-8)/method_layout_.size)
  return deny("interpretation-compilation-fixture-array-unmapped");
 size_t yes_count=0,no_count=0;
 for(uint32_t i=0;i<methods.count;++i){void*method=methods.At(i);const uint32_t flags=method_metadata::ReadAccessFlags(method);
  yes_count+=method==yes;no_count+=method==no;
  if(!(flags&kNotManagedInvokable)&&(flags&kDontCompile))return deny("interpretation-compilation-fixture-has-existing-dontcompile-state");
 }
 const uint32_t yes_flags=method_metadata::ReadAccessFlags(yes),no_flags=method_metadata::ReadAccessFlags(no);
 if(yes_count!=1||no_count!=1||((yes_flags|no_flags)&(kNotManagedInvokable|kIntrinsic|kPreCompiled)))
  return deny("interpretation-compilation-probes-are-not-ordinary-managed-array-members");
 ProbeScope scope(*this,yes);
 struct ProbeFlags {
  void*yes;void*no;
  ProbeFlags(void*a,void*b):yes(a),no(b){
   __atomic_fetch_or(Flags(a),kDontCompile,__ATOMIC_RELAXED);__atomic_fetch_or(Flags(b),kDontCompile,__ATOMIC_RELAXED);}
  ~ProbeFlags(){__atomic_fetch_and(Flags(yes),~kDontCompile,__ATOMIC_RELAXED);__atomic_fetch_and(Flags(no),~kDontCompile,__ATOMIC_RELAXED);}
 } flags(yes,no);
 clear_intrinsic_(yes); // The ordinary negative for ART's intrinsic overlay conversion.
 clear_class_target_(klass,PointerSize::k64);
 if(compilation_probe_calls!=1||method_metadata::ReadAccessFlags(yes)!=(yes_flags|kDontCompile)||
    method_metadata::ReadAccessFlags(no)!=no_flags||!stop.IsStopped(self)||*runtime_slot_!=runtime_)
  return deny("interpretation-compilation-class-wide-positive-negative-behavior-unproved");
 compilation_calibrated_=true;return true;
}
void Registry::ForceNonCompilable(void*method)const noexcept{
 if(!method||!clear_intrinsic_||!compilation_calibrated_)std::abort();
 if(method_metadata::ReadAccessFlags(method)&kIntrinsic)clear_intrinsic_(method);
 uint32_t flags=__atomic_load_n(Flags(method),__ATOMIC_RELAXED);
 for(;;){const uint32_t next=(flags&~kPreCompiled)|kDontCompile;
  if(__atomic_compare_exchange_n(Flags(method),&flags,next,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED))break;
 }
}
bool Registry::BindEntryQuery(HookEngine&hooks,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!runtime_slot_||*runtime_slot_!=runtime_||!class_linker_||!instrumentation_)
  return deny("interpretation-code-query-runtime-owner-unavailable");
 if(code_target_)return is_bridge_&&reinitialize_?true:deny("interpretation-code-query-bridge-or-reinitialization-unavailable");
 auto code=reinterpret_cast<CodeQuery>(hooks.FindSymbol("libart.so",kCodeQuery));
 auto bridge=reinterpret_cast<IsBridge>(hooks.FindSymbol("libart.so",kIsBridge));
 auto reinitialize=reinterpret_cast<Reinitialize>(hooks.FindSymbol("libart.so",kReinitialize));
 if(!code||!bridge||!reinitialize)return deny("interpretation-code-query-named-original-or-bridge-predicate-unavailable");
 code_target_=code;is_bridge_=bridge;reinitialize_=reinitialize;return true;
}
bool Registry::BindEntryUpdates(HookEngine&hooks,std::string*error){
 if(!runtime_slot_||*runtime_slot_!=runtime_||!instrumentation_||!reinitialize_){
  if(error)*error="interpretation-entry-updates-runtime-or-reinitialization-unavailable";return false;
 }
 std::array<void*,7>targets{};
 for(size_t i=0;i<targets.size();++i){
  targets[i]=hooks.FindSymbol("libart.so",kEntryUpdates[i]);
  if(!targets[i]){if(error)*error="interpretation-entry-updates-named-original-unavailable";return false;}
  for(size_t j=0;j<i;++j)if(targets[i]==targets[j]){
   if(error)*error="interpretation-entry-updates-distinct-signatures-share-target";return false;
  }
 }
 if(update_targets_[0]&&update_targets_!=targets){if(error)*error="interpretation-entry-updates-original-owner-changed";return false;}
 update_targets_=targets;return true;
}
bool Registry::InstallEntryUpdates(HookEngine&hooks,const native_source_stop::Session&stop,void*self,std::string*error){
 if(!stop.IsStopped(self)||!runtime_slot_||*runtime_slot_!=runtime_||query_owner.load(std::memory_order_acquire)!=this||
    !query_calibrated_||!compilation_calibrated_||!code_calibrated_||!reinitialize_||!interpreter_bridge_){
  if(error)*error="interpretation-entry-updates-installation-needs-calibrated-owner-and-stop";return false;
 }
 const std::array<void*,7>replacements{{reinterpret_cast<void*>(EntryUpdateHooks::Debuggable),
  reinterpret_cast<void*>(EntryUpdateHooks::Disable),reinterpret_cast<void*>(EntryUpdateHooks::Key<2>),
  reinterpret_cast<void*>(EntryUpdateHooks::Key<3>),reinterpret_cast<void*>(EntryUpdateHooks::Key<4>),
  reinterpret_cast<void*>(EntryUpdateHooks::Trace),reinterpret_cast<void*>(EntryUpdateHooks::Key<6>)}};
 for(size_t i=0;i<update_tokens_.size();++i){
  if(!update_targets_[i]){if(error)*error="interpretation-entry-updates-originals-not-bound";return false;}
  if(update_tokens_[i])continue;
  update_tokens_[i]=hooks.Install(update_targets_[i],replacements[i],&update_originals_[i],error);
  if(!update_tokens_[i]){if(update_originals_[i])std::abort();return false;}
  if(!update_originals_[i]||update_originals_[i]==update_targets_[i]||!stop.IsStopped(self))std::abort();
 }
 query_owner.store(this,std::memory_order_release);return true;
}
void Registry::RestoreProtectedEntries(void*instrumentation)const noexcept{
 if(instrumentation!=instrumentation_||!runtime_slot_||*runtime_slot_!=runtime_)return;
 // These fixed original operations retain exclusive mutator ownership for
 // their entire native callback. Publication and allocator pruning therefore
 // cannot change this array until we return. Do not hold the native registry
 // mutex across ReinitializeMethodsCode: its query calls Contains again.
 const auto restore=[&](void*method){
  if(method_metadata::ReadAccessFlags(method)&0x40000u)return;
  ForceNonCompilable(method);reinitialize_(instrumentation_,method);
  const auto entry=__atomic_load_n(reinterpret_cast<const uintptr_t*>(static_cast<const unsigned char*>(method)+method_layout_.entry_point),__ATOMIC_RELAXED);
  if(entry!=reinterpret_cast<uintptr_t>(interpreter_bridge_))std::abort();
 };
 for(void*method:methods_)restore(method);
 if(probe_owner==this&&probe_method&&!Has(methods_,probe_method))restore(probe_method);
}
bool Registry::CalibrateEntryQuery(HookEngine&hooks,const native_source_stop::Session&stop,void*self,
 native_selective_interpretation::Service&positive,const native_selective_interpretation::Service&negative,
 const modern_field_metadata::ReadContext&read,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(!query_calibrated_||!compilation_calibrated_||!class_linker_||!code_target_||!is_bridge_||!reinitialize_||
    positive.phase_!=native_selective_interpretation::Service::Phase::Prepared||!positive.undeopt_||positive.SelectedCount()!=1)
  return deny("interpretation-code-query-independent-prepared-probes-unavailable");
 const char*failure=nullptr;
 RootedProbes pair{};
 if(!ValidateProbePair(stop,self,positive,negative,read,&pair,&failure))
  return deny(failure?failure:"interpretation-code-query-probe-identities-unproved");
 void*yes=pair.positive;void*no=pair.negative;
 const uint32_t saved_flags=method_metadata::ReadAccessFlags(yes);
 if(saved_flags&(kIntrinsic|kPreCompiled|kDontCompile))
  return deny("interpretation-code-query-probe-has-existing-compilation-overlay");
 const auto original=code_token_?original_code_:code_target_;
 if(!original)return deny("interpretation-code-query-original-forwarder-unavailable");
 const void*negative_code=original(instrumentation_,no);
 if(!negative_code)return deny("interpretation-code-query-negative-has-no-original-code");
 // Declare refund before activation and before the temporary probe. Exception
 // unwinding removes only this actual native entry, after the probe has ended.
 // Fixture declarations/DEX stay installed; this grants no private disposal.
 struct Refund {
  native_selective_interpretation::Service&selection;
  const native_source_stop::Session&stop;void*self;
  const modern_field_metadata::ReadContext&read;bool done=false;
  void Close()noexcept{if(done)return;native_selective_interpretation::RefundReport report{};
   if(!selection.Refund(stop,self,read,&report)||!report.complete||selection.PendingRefund())std::abort();done=true;}
  ~Refund(){Close();}
 } refund{positive,stop,self,read};
 struct CompileFlag {
  void*method;bool done=false;
  explicit CompileFlag(void*value):method(value){__atomic_fetch_or(Flags(method),kDontCompile,__ATOMIC_RELAXED);}
  void Close()noexcept{if(!done){__atomic_fetch_and(Flags(method),~kDontCompile,__ATOMIC_RELAXED);done=true;}}
  ~CompileFlag(){Close();}
 } compile_flag(yes);
 native_selective_interpretation::Report activated{};
 if(!positive.Activate(stop,self,read,&activated))return deny(activated.failure?activated.failure:"interpretation-code-query-original-activation-failed");
 if(activated.activated!=1||activated.already_interpreted||positive.PendingRefund()!=1)
  return deny("interpretation-code-query-probe-did-not-own-one-original-entry");
 native_selective_interpretation::Report retained{};
 if(!positive.ValidateRetained(stop,self,read,&retained))return deny(retained.failure?retained.failure:"interpretation-code-query-original-entry-not-retained");
 const void*bridge=original(instrumentation_,yes);
 if(!bridge||!is_bridge_(class_linker_,bridge)||(interpreter_bridge_&&interpreter_bridge_!=bridge)||
    !stop.IsStopped(self)||*runtime_slot_!=runtime_)
  return deny("interpretation-code-query-original-deopt-bridge-unproved");
 // Authenticate the fixed mutable entry field against the original operation,
 // not a public identity prefix or a guessed code address.
 const auto layout=positive.MethodLayout();size_t extent=0;
 if(!read.owner||!read.extent||!read.extent(read.owner,yes,&extent)||
    extent<layout.entry_point||extent-layout.entry_point<sizeof(uintptr_t)||
    uintptr_t(yes)>UINTPTR_MAX-layout.entry_point||
    __atomic_load_n(reinterpret_cast<const uintptr_t*>(static_cast<const unsigned char*>(yes)+layout.entry_point),__ATOMIC_RELAXED)!=uintptr_t(bridge))
  return deny("interpretation-code-query-original-entry-field-disagrees-with-bridge");
 interpreter_bridge_=bridge;
 if(!code_token_){
  code_token_=hooks.Install(reinterpret_cast<void*>(code_target_),reinterpret_cast<void*>(EntryQueryHooks::Invoke),
   reinterpret_cast<void**>(&original_code_),error);
  if(!code_token_){original_code_=nullptr;return false;}
  if(!original_code_||original_code_==code_target_||!stop.IsStopped(self))std::abort();
  query_owner.store(this,std::memory_order_release);
 }
 bool matched=false;
 {
  ProbeScope probe(*this,yes);
  const void*selected=code_target_(instrumentation_,yes);const size_t hits=code_probe_calls;
  const void*unselected=code_target_(instrumentation_,no);
  matched=selected==bridge&&hits==1&&code_probe_calls==hits&&unselected==negative_code&&
   stop.IsStopped(self)&&*runtime_slot_==runtime_&&original_query_(instrumentation_,yes)&&
   !original_query_(instrumentation_,no);
  // Exercise actual ownership handoff: original Undeoptimize removes its set
  // entry while persistent policy retains the bridge. Reinitialization must
  // consume that policy after native ownership is gone.
  refund.Close();
  const auto entry=[&]{return __atomic_load_n(reinterpret_cast<const uintptr_t*>(
   static_cast<const unsigned char*>(yes)+layout.entry_point),__ATOMIC_RELAXED);};
  matched=matched&&!original_query_(instrumentation_,yes)&&entry()==uintptr_t(bridge);
  const size_t query_hits=probe_calls;
  reinitialize_(instrumentation_,yes);
  matched=matched&&probe_calls>query_hits&&entry()==uintptr_t(bridge)&&
   method_metadata::ReadAccessFlags(yes)==(saved_flags|kDontCompile)&&stop.IsStopped(self)&&*runtime_slot_==runtime_;
 }
 compile_flag.Close();
 // Restore the ordinary fixture's entry policy after ending temporary
 // ownership. Its declarations/DEX remain installed throughout calibration.
 reinitialize_(instrumentation_,yes);
 if(!matched)return deny("interpretation-code-query-forwarding-or-original-handoff-unproved");
 if(original_query_(instrumentation_,yes)||original_query_(instrumentation_,no))
  return deny("interpretation-code-query-temporary-native-entry-not-refunded");
 code_calibrated_=true;return true;
}
std::unique_ptr<Registry::Reservation>Registry::Prepare(const std::vector<void*>&methods,std::string*error){
 const auto deny=[&](const char*why)->std::unique_ptr<Reservation>{if(error)*error=why;return nullptr;};
 auto result=std::unique_ptr<Reservation>(new Reservation(*this));
 result->requested_=methods;
 if(std::find(methods.begin(),methods.end(),nullptr)!=methods.end())
  return deny("interpretation-registry-null-carrier");
 std::sort(result->requested_.begin(),result->requested_.end(),Less);
 if(std::adjacent_find(result->requested_.begin(),result->requested_.end())!=result->requested_.end())
  return deny("interpretation-registry-duplicate-carrier");
 // Declare the lock after result so a failed preparation unlocks before its
 // Reservation destructor dispatches Cancel. No recursive native mutex is used.
 std::lock_guard<std::mutex>lock(mutex_);
 if(pending_)return deny("interpretation-registry-publication-already-reserved");
 if(result->requested_.size()>result->candidate_.max_size()-methods_.size())
  return deny("interpretation-registry-carrier-capacity-overflow");
 result->candidate_.reserve(methods_.size()+result->requested_.size());
 std::set_union(methods_.begin(),methods_.end(),result->requested_.begin(),result->requested_.end(),
                std::back_inserter(result->candidate_),Less);
 result->valid_=true;pending_=result.get();return result;
}
bool Registry::Contains(void*method)const noexcept{
 std::lock_guard<std::mutex>lock(mutex_);return Has(methods_,method);
}
size_t Registry::Count()const noexcept{
 std::lock_guard<std::mutex>lock(mutex_);return methods_.size();
}
bool Registry::PreparedFor(const Reservation&reservation,const std::vector<void*>&methods)const noexcept{
 std::lock_guard<std::mutex>lock(mutex_);
 if(&reservation.owner_!=this||pending_!=&reservation||!reservation.valid_||reservation.committed_||
    methods.size()!=reservation.requested_.size())return false;
 // Callers need not carry the registry's ordering. Check a bijection without
 // allocating inside the source stop, including duplicate supplied carriers.
 for(size_t i=0;i<methods.size();++i){
  if(!Has(reservation.requested_,methods[i])||!Has(reservation.candidate_,methods[i]))return false;
  for(size_t j=0;j<i;++j)if(methods[j]==methods[i])return false;
 }
 return true;
}
Registry::Query Registry::NativeQueryFor(void*runtime,void*instrumentation)noexcept{
 auto*owner=query_owner.load(std::memory_order_acquire);
 return owner&&owner->query_token_&&owner->runtime_==runtime&&owner->instrumentation_==instrumentation&&
  owner->runtime_slot_&&*owner->runtime_slot_==runtime?owner->original_query_:nullptr;
}
bool Registry::ReadyFor(const native_selective_interpretation::Service&selection)const noexcept{
 return query_owner.load(std::memory_order_acquire)==this&&query_calibrated_&&compilation_calibrated_&&code_calibrated_&&
  std::all_of(update_tokens_.begin(),update_tokens_.end(),[](void*token){return token!=nullptr;})&&
  query_token_&&compilation_token_&&code_token_&&interpreter_bridge_&&original_query_&&
  runtime_slot_&&*runtime_slot_==runtime_&&retirement::HasInterpretationCleanup(*this)&&
  selection.PreparedSelection()&&selection.runtime_==runtime_&&selection.runtime_slot_==runtime_slot_&&
  selection.instrumentation_==instrumentation_&&SameLayout(selection.MethodLayout(),method_layout_)&&
  selection.metadata_&&selection.metadata_->StorageModel()==class_model_;
}
bool Registry::Commit(const native_source_stop::Session&stop,void*self,Reservation&reservation)noexcept{
 if(!stop.IsFinalWindow(self)||!runtime_slot_||*runtime_slot_!=runtime_||!query_calibrated_||!compilation_calibrated_||!code_calibrated_||
    !std::all_of(update_tokens_.begin(),update_tokens_.end(),[](void*token){return token!=nullptr;})||
    query_owner.load(std::memory_order_acquire)!=this||probe_owner||!retirement::HasInterpretationCleanup(*this))return false;
 std::unique_lock<std::mutex>lock(mutex_);
 if(&reservation.owner_!=this||pending_!=&reservation||!reservation.valid_||reservation.committed_)return false;
 // The private execution caller has just checked these exact rooted carriers
 // and the complete entry protection in this same final stop. Check the
 // invokable Java predicate before publishing any key or changing any flag.
 for(void*method:reservation.requested_)
  if(method_metadata::ReadAccessFlags(method)&kNotManagedInvokable)return false;
 // Old obsolete carriers already have ART's retirement entry and never need
 // a persistent interpreter query. Earlier successful callers can also become
 // obsolete in this operation. Their tracked allocations are still live in
 // this exclusive window; allocator pruning precedes their destruction.
 reservation.candidate_.erase(std::remove_if(reservation.candidate_.begin(),reservation.candidate_.end(),
  [](void*method){return (method_metadata::ReadAccessFlags(method)&0x40000u)!=0;}),reservation.candidate_.end());
 methods_.swap(reservation.candidate_);reservation.committed_=true;
 reservation.valid_=false;pending_=nullptr;
 lock.unlock();
 // No managed thread or allocator cleanup can run during this actual final
 // exclusive window. Never hold our native mutex across an original ART leaf.
 for(void*method:reservation.requested_)
  if(!(method_metadata::ReadAccessFlags(method)&0x40000u))ForceNonCompilable(method);
 if(!stop.IsFinalWindow(self)||*runtime_slot_!=runtime_)std::abort();
 return true;
}
void Registry::Cancel(Reservation&reservation)noexcept{
 std::lock_guard<std::mutex>lock(mutex_);
 if(&reservation.owner_!=this)std::abort();
 if(pending_==&reservation)pending_=nullptr;
 reservation.valid_=false;
}
size_t Registry::Prune(const void*allocator,ContainsUnsafe contains)noexcept{
 if(!allocator||!contains)std::abort();
 std::lock_guard<std::mutex>lock(mutex_);
 const auto belongs=[&](void*method){return contains(allocator,method);};
 const size_t before=methods_.size();
 // The destructor observer also sees foreign compiler arenas on native
 // threads. They contain no Class method carrier, so leave vector storage
 // completely untouched; even erase(end,end) can write its end pointer.
 // Matching Class arenas still follow ART's original mutator-protected
 // destruction schedule, serialized with exclusive entry reconstruction.
 if(std::any_of(methods_.begin(),methods_.end(),belongs))
  methods_.erase(std::remove_if(methods_.begin(),methods_.end(),belongs),methods_.end());
 if(pending_){
  for(void*method:pending_->requested_)if(belongs(method)){pending_->valid_=false;break;}
  // A collector may unload an unrelated earlier carrier while ordinary
  // preparation is pending. Never resurrect that address through a later swap.
  auto&candidate=pending_->candidate_;
  if(std::any_of(candidate.begin(),candidate.end(),belongs))
   candidate.erase(std::remove_if(candidate.begin(),candidate.end(),belongs),candidate.end());
 }
 return before-methods_.size();
}
}

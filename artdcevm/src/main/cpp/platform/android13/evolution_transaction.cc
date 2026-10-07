#include "platform/android13/evolution_transaction.h"
#include "platform/android13/evolution_transaction_abi.h"
#include "platform/android13/device_dex_input.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/instrumentation_listener.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/frame_recovery.h"
#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#if defined(ARTDCEVM_TESTING) && defined(__ANDROID__)
#include <android/log.h>
#define DRAIN_LOG(...) __android_log_print(ANDROID_LOG_ERROR,"ArtDcevmRecovery",__VA_ARGS__)
#else
#define DRAIN_LOG(...) ((void)0)
#endif
namespace artdcevm::android13 {
namespace {
using Class=validation_abi::ClassPrefix;
using Redefine=jvmtiError(*)(jvmtiEnv*,jint,const jvmtiClassDefinition*);
Redefine redefine=nullptr;
void*(*lookup)(void*,void*,const char*,void*)=nullptr;
void** list_lock=nullptr;void** runtime_instance=nullptr;
void(*lock)(void*,void*)=nullptr;void(*unlock)(void*,void*)=nullptr;
void(*for_each)(void*,void(*)(void*,void*),void*)=nullptr;
uintptr_t(*return_pc)(const void*)=nullptr;
bool(*contains_pc)(const void*,const void*)=nullptr;
bool(*private_contains_pc)(const void*,const void*)=nullptr;
void*(*read_root)(void*)=nullptr;
void*(*obsolete_cache)(void*)=nullptr;
const void*(*oat_code)(void*,size_t)=nullptr;
bool(*debuggable)(const void*)=nullptr;
std::atomic<uint64_t> guard_counts[3]{},compiled_counts[6]{};
void Require(bool value) { if(!value) std::abort(); }
template<class T> T Read(const void* p,size_t at=0) { T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value; }
void* Parent(void* klass) {return ReadHeapReference(klass,offsetof(Class,super_class));}
void* Loader(void* klass) {return ReadHeapReference(klass,offsetof(Class,class_loader));}
struct InputHierarchy {
  validation_abi::DexMappingPrefix map;
  bool Range(size_t at,size_t width,size_t size) const {return at<=size && width<=size-at;}
  bool U32(const unsigned char* bytes,size_t size,size_t at,uint32_t* value) const {
    if(!bytes || !Range(at,4,size)) return false;
    *value=uint32_t{bytes[at]}|uint32_t{bytes[at+1]}<<8|uint32_t{bytes[at+2]}<<16|uint32_t{bytes[at+3]}<<24;return true;
  }
  bool Main(size_t at,uint32_t* value) const {return U32(map.begin,map.size,at,value);}
  bool Id(size_t header,uint32_t index,size_t width,size_t* at) const {
    uint32_t count,offset;
    if(!Main(header,&count) || !Main(header+4,&offset) || index>=count || offset<0x70 || offset%4 || !Range(offset,size_t{count}*width,map.size)) return false;
    *at=offset+size_t{index}*width;return true;
  }
  bool Type(uint16_t index,const char** value) const {
    size_t at;uint32_t string;
    if(index==UINT16_MAX || !Id(64,index,4,&at) || !Main(at,&string) || !Id(56,string,4,&at)) return false;
    uint32_t data_at;if(!Main(at,&data_at)) return false;at=data_at;bool complete=false;
    for(unsigned i=0;i<5;++i) {
      if(!map.data_begin || !Range(at,1,map.data_size)) return false;uint8_t byte=map.data_begin[at++];
      if(i==4 && (byte&0xf0)) return false;
      if(!(byte&0x80)) {complete=true;break;}
    }
    if(!complete || !Range(at,1,map.data_size) || !std::memchr(map.data_begin+at,0,map.data_size-at)) return false;
    *value=reinterpret_cast<const char*>(map.data_begin+at);return true;
  }
  bool ReadHierarchy(std::string* parent,std::vector<std::string>* interfaces) const {
    uint32_t count;size_t def;
    if(!Main(96,&count) || count!=1 || !Id(96,0,32,&def) || !Range(def+8,2,map.size)) return false;
    uint16_t type=uint16_t(map.begin[def+8]|uint16_t{map.begin[def+9]}<<8);const char* name;
    if(!Type(type,&name)) return false;*parent=name;
    uint32_t list;if(!Main(def+12,&list)) return false;
    if(!list) return true;
    if(!U32(map.data_begin,map.data_size,list,&count) || !Range(size_t{list}+4,size_t{count}*2,map.data_size)) return false;
    interfaces->reserve(count);
    for(size_t i=0;i<count;++i) {
      size_t at=size_t{list}+4+2*i;uint16_t next=uint16_t(map.data_begin[at]|uint16_t{map.data_begin[at+1]}<<8);
      if(!Type(next,&name)) return false;interfaces->emplace_back(name);
    }
    return true;
  }
};
enum class Scan {kNone,kMarked,kUnsupported};
struct ActiveScan { void* self;void* marked=nullptr;Scan result=Scan::kNone;uint64_t token=0; };
struct Visitor final:stack_abi::Visitor {
  Visitor(void* self,ActiveScan& state):stack_abi::Visitor(self,nullptr,stack_abi::WalkKind::kIncludeInlined),state(state) {}
  bool VisitFrame() override {
    if(!quick_frame) return true;
    bool physical=quick_frame!=previous_quick;previous_quick=quick_frame;
    void* method=Method();if(!method) return true;
    uint32_t flags=art_method_abi::ReadAccessFlags(method);
    bool runtime=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX;
    bool native=(flags&0x100)!=0;
    bool barrier=physical && !runtime && selective_abi::FindFrame(Read<void*>(thread,transaction_abi::kThreadInstrumentationStack),return_pc(this));
    void* cache=RuntimeJitCodeCache();
    bool app=!runtime && !native && Loader(read_root(method))!=nullptr;
    bool carrier=physical && !runtime && !native && !app && header && cache &&
        contains_pc(cache,reinterpret_cast<const void*>(pc));
    if(carrier) {stack_abi::CodeInfo info{};carrier=ReadEvolutionCodeInfo(Read<void*>(quick_frame),header,pc,&info) && info.inline_infos.rows!=0;}
    if(runtime || native || !header || (!app && !carrier)) {barrier_above|=barrier;return true;}
    if(carrier) ++compiled_counts[5];
    const void* code=oat_code(method,8);
    bool aot=code && header==static_cast<const unsigned char*>(code)-stack_abi::kCodeOffset;
    if(barrier_above) {++compiled_counts[2];if(aot) ++compiled_counts[1];return true;}
    if(carrier && !private_contains_pc(cache,reinterpret_cast<const void*>(pc))) return Unsupported("nonprivate-carrier");
    if(aot) {
      void* klass=read_root(method);
      void* dex_cache=(flags&0x40000)?obsolete_cache(method):reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(klass,offsetof(Class,dex_cache))});
      const void* dex=dex_cache?reinterpret_cast<void*>(Read<uint64_t>(dex_cache,offsetof(validation_abi::DexCachePrefix,dex_file))):nullptr;
      const void* oat_dex=dex?Read<const void*>(dex,offsetof(stack_abi::DexPrefix,oat_dex)):nullptr;
      const void* oat=oat_dex?Read<const void*>(oat_dex):nullptr;
      if(!oat || !debuggable(oat)) return Unsupported("nondebug-aot");
    }
    if(!IsEvolutionPcRecoverable(thread,pc)) return Unsupported("pc-recovery");
    if(!MarkEvolutionSuspendDeopt(thread,&state.token)) return Unsupported("mark-busy");
    state.marked=thread;state.result=Scan::kMarked;++compiled_counts[0];barrier_above|=barrier;return false;
  }
  bool Unsupported(const char* reason) {(void)reason;DRAIN_LOG("drain-reject=%s thread=%p method=%p pc=%zx",reason,thread,Method(),size_t(pc));state.result=Scan::kUnsupported;++compiled_counts[4];return false;}
  ActiveScan& state;bool barrier_above=false;void** previous_quick=nullptr;
};
struct RecoveryVisitor final:stack_abi::Visitor {
  explicit RecoveryVisitor(void* thread):Visitor(thread,nullptr,stack_abi::WalkKind::kIncludeInlined) {}
  bool Flush() {return !app_carrier || maps_safe;}
  bool VisitFrame() override {
    if(quick_frame!=carrier) {
      if(!Flush()) {safe=false;return false;}
      carrier=quick_frame;app_carrier=false;maps_safe=true;
    }
    if(!quick_frame) return true;
    void* method=Method();if(!method) return true;
    if(Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
       (art_method_abi::ReadAccessFlags(method)&0x100)) return true;
    // Every inlinee AND its physical carrier must have recoverable maps when
    // this machine frame contains an application method, including boot carriers.
    app_carrier|=Loader(read_root(method))!=nullptr;
    maps_safe&=IsEvolutionFrameRecoverable(*this);
    return true;
  }
  void** carrier=nullptr;bool app_carrier=false,maps_safe=true,safe=true;
};
void CheckRecoveryThread(void* thread,void* raw) {
  auto* safe=static_cast<bool*>(raw);if(!*safe) return;
  RecoveryVisitor visitor(thread);visitor.Walk();*safe=visitor.safe&&visitor.Flush();
}
void ScanThread(void* thread,void* raw) {
  auto& state=*static_cast<ActiveScan*>(raw);
  if(state.result!=Scan::kNone || thread==state.self) return;
  Visitor visitor(thread,state);visitor.Walk();
}
Scan MarkActive(void** marked,uint64_t* token) {
  void* self=CurrentHierarchyThread();ActiveScan state{self};
  NativeSuspendScope native(self);Require(native.Supported());
  { OwnedSuspendAll suspended("ART DCEVM active compiled precommit check");
    lock(*list_lock,self);
    // All scan state is native and no JNI roots are created while suspended.
    for_each(Read<void*>(*runtime_instance,offsetof(runtime_access_abi::RuntimePrefix,thread_list)),ScanThread,&state);
    unlock(*list_lock,self);
  }
  *marked=state.marked;*token=state.token;if(state.result==Scan::kNone) ++compiled_counts[3];return state.result;
}
class HierarchyStateScope {
 public:HierarchyStateScope():previous(BeginHierarchyTransaction()) {}
 ~HierarchyStateScope() {EndHierarchyTransaction(previous);}
 private:uint32_t previous;
};
}
bool CheckEvolutionCompiledFramesRecoverable() {
  void* self=CurrentHierarchyThread();bool safe=true;
  lock(*list_lock,self);
  for_each(Read<void*>(*runtime_instance,offsetof(runtime_access_abi::RuntimePrefix,thread_list)),CheckRecoveryThread,&safe);
  unlock(*list_lock,self);
  // One failed preflight is one unsupported decision, regardless of how many
  // threads/inline frames the suspended scan observed before rejecting it.
  if(!safe) ++compiled_counts[4];
  return safe;
}
bool NeedsEvolutionReceiverGuard(jint count,const jvmtiClassDefinition* definitions) {
  if(count<=0 || !definitions) return false;
  for(jint i=0;i<count;++i) {
    const auto& def=definitions[i];
    if(def.class_byte_count<=0 || !def.class_bytes || !def.klass) continue;
    DeviceDexInput input(def.class_bytes,static_cast<size_t>(def.class_byte_count));
    if(!input.Valid()) continue; // Device verifier owns final rejection semantics.
    std::string parent;std::vector<std::string> interfaces;
    if(!InputHierarchy{input.Mapping()}.ReadHierarchy(&parent,&interfaces)) continue;
    NativeObjectAccess access(ValidationHooks::TransactionJni());Require(access.Supported());
    Require(interfaces.size()<=static_cast<size_t>(INT32_MAX)-4);
    HandleRootScope roots(access.Self(),4+static_cast<uint32_t>(interfaces.size()));
    roots.Set(0,access.Decode(def.klass));if(!roots.Get(0) || !Parent(roots.Get(0))) continue;
    roots.Set(1,Loader(roots.Get(0)));
    roots.Set(2,lookup(RuntimeClassLinker(),access.Self(),parent.c_str(),roots.Get(1)));
    if(!roots.Get(2) || !IsValidationSubclassOf(roots.Get(2),Parent(roots.Get(0)))) return true;
    void* table=ReadHeapReference(roots.Get(0),offsetof(Class,iftable));
    int32_t old_count=table?Read<int32_t>(table,8)/2:0;
    for(int32_t old=0;old<old_count;++old) {
      table=ReadHeapReference(roots.Get(0),offsetof(Class,iftable));
      roots.Set(3,ReadHeapReference(table,12+8*static_cast<size_t>(old)));
      bool retained=IsValidationAssignableFrom(roots.Get(3),roots.Get(2));
      for(size_t next=0;next<interfaces.size();++next) {
        roots.Set(4+next,lookup(RuntimeClassLinker(),access.Self(),interfaces[next].c_str(),roots.Get(1)));
        if(!roots.Get(4+next)) return true;
        retained|=IsValidationAssignableFrom(roots.Get(3),roots.Get(4+next));
      }
      if(!retained) return true;
    }
  }
  return false;
}
bool DrainEvolutionCompiledFrames() {
  // A previous timed-out execution owns its slot and protection until the
  // interpreter entry acknowledges its converted anchor. Never reuse it early.
  if(EvolutionSuspendDeoptInFlight()) {DRAIN_LOG("drain-reject=inflight");return false;}
  for(int frame=0;frame<64;++frame) {
    void* marked=nullptr;uint64_t token=0;Scan scan=MarkActive(&marked,&token);
    if(scan==Scan::kNone) return true;
    if(scan==Scan::kUnsupported) return false;
    for(int ms=0;ms<100 && EvolutionSuspendDeoptPending(marked,token);++ms) usleep(1000);
    if(EvolutionSuspendDeoptPending(marked,token)) {
      // Cancellation only succeeds while queued. Claimed/Executing are retained:
      // the caller must not roll back protection while a deopt is in progress.
      DRAIN_LOG("drain-reject=timeout thread=%p token=%llu phase=%u",marked,static_cast<unsigned long long>(token),static_cast<unsigned>(EvolutionSuspendDeoptState(marked,token)));
      CancelEvolutionSuspendDeopt(marked,token);
      if(EvolutionSuspendDeoptState(marked,token)==SuspendDeoptState::kRejected)
        ReleaseEvolutionSuspendDeopt(marked,token);
      return false;
    }
    auto state=EvolutionSuspendDeoptState(marked,token);
    if(state!=SuspendDeoptState::kCompleted) {
      DRAIN_LOG("drain-reject=not-completed thread=%p token=%llu phase=%u",marked,static_cast<unsigned long long>(token),static_cast<unsigned>(state));
      ReleaseEvolutionSuspendDeopt(marked,token);return false;
    }
    if(!ReleaseEvolutionSuspendDeopt(marked,token)) {DRAIN_LOG("drain-reject=release-mismatch");return false;}
    // Ack covers the chosen physical anchor, not every lower quick fragment.
    usleep(1000);
  }
  DRAIN_LOG("drain-reject=64-anchors");return false;
}
jvmtiError RedefineEvolutionTransaction(jvmtiEnv* env,jint count,const jvmtiClassDefinition* definitions,bool transform) {
  HierarchyStateScope scope;
  bool needs=transform || NeedsEvolutionReceiverGuard(count,definitions);
  bool already=ReceiverGuardEnabled(),previous=ReceiverGuardRequired();
  // A newly enabled listener checks maps atomically with installation. Only
  // the already-enabled path needs an independent transaction preflight.
  if(needs && already) {
    NativeSuspendScope native(CurrentHierarchyThread());Require(native.Supported());
    OwnedSuspendAll suspended("ART DCEVM compiled recovery preflight");
    if(!CheckEvolutionCompiledFramesRecoverable()) return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  }
  if(needs && !already && !SetReceiverGuardMode(true,false))
    return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  if(needs) SetReceiverGuardRequired(true);
  if(needs && !already) ++guard_counts[0];
  jvmtiError result=needs && !DrainEvolutionCompiledFrames()?JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED:redefine(env,count,definitions);
  if(needs) {
    if(result==JVMTI_ERROR_NONE && HasHierarchyNarrowing()) ++guard_counts[2];
    else if(!EvolutionSuspendDeoptInFlight()) {
      SetReceiverGuardRequired(previous);
      if(!already) {SetReceiverGuardMode(false,false);if(result!=JVMTI_ERROR_NONE) ++guard_counts[1];}
    }
  }
  return result;
}
uint64_t EvolutionGuardTransactionDiagnostics(int index) {return index>=0 && index<3?guard_counts[index].load():0;}
uint64_t EvolutionCompiledPrecommitDiagnostics(int index) {return index>=0 && index<6?compiled_counts[index].load():0;}
bool ConfigureEvolutionTransactions(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
#define ART(dst,name) dst=reinterpret_cast<decltype(dst)>(hooks.FindSymbol("libart.so",name))
  ART(lookup,cas::kLookupClass);ART(runtime_instance,"_ZN3art7Runtime9instance_E");ART(list_lock,"_ZN3art5Locks17thread_list_lock_E");
  ART(lock,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");ART(unlock,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
  ART(for_each,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");
  ART(return_pc,"_ZNK3art12StackVisitor15GetReturnPcAddrEv");ART(contains_pc,"_ZNK3art3jit12JitCodeCache10ContainsPcEPKv");
  ART(private_contains_pc,"_ZNK3art3jit12JitCodeCache23PrivateRegionContainsPcEPKv");ART(read_root,"artReadBarrierForRootSlow");
  ART(obsolete_cache,"_ZN3art9ArtMethod19GetObsoleteDexCacheEv");ART(oat_code,"_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE");
  ART(debuggable,"_ZNK3art7OatFile12IsDebuggableEv");
#undef ART
  redefine=reinterpret_cast<Redefine>(hooks.FindSymbol("libopenjdkjvmti.so",cas::kStructuralRedefineClasses));
  if(!lookup || !runtime_instance || !*runtime_instance || !list_lock || !lock || !unlock || !for_each || !return_pc || !contains_pc || !private_contains_pc || !read_root || !obsolete_cache || !oat_code || !debuggable || !redefine) {
    *error="Missing named transaction preflight, compiled-frame or redefinition capability";return false;
  }
  if(!ConfigureDeviceDexInputs(hooks,profile,error)) return false;
  return true;
}
}

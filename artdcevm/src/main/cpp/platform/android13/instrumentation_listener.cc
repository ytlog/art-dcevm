#include "platform/android13/instrumentation_listener.h"
#include "platform/android13/instrumentation_listener_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/thread_suspend_scope.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#ifdef __ANDROID__
#include <android/log.h>
#endif
namespace artdcevm::android13 {
namespace {
using namespace listener_abi;
using Class=validation_abi::ClassPrefix;
using Method=art_method_abi::ArtMethod;
void* (*current)();
void* (*read_root)(void*);
void* (*obsolete_cache)(void*);
void* (*resolve_type)(void*,uint16_t,void*);
const char* (*field_name)(void*);
bool (*proxy_descriptor)(void*,const char*);
void (*throw_exception)(void*,const char*,const char*);
void (*add_listener)(void*,Listener*,uint32_t),(*remove_listener)(void*,Listener*,uint32_t);
void (*deopt_all)(void*,const char*),(*undeopt_all)(void*,const char*);
std::atomic<uint64_t> field_hits{0},invalid_hits{0},invoke_checks{0},invoke_rejections{0},frame_misses{0};
std::atomic<bool> tracing{false};
bool force_reject=false; // Published and modified only while every mutator is suspended.
template<class T> T Read(const void* p,size_t offset=0) { T value; std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value)); return value; }
void Require(bool condition) { if (!condition) std::abort(); }
void* Cache(void* method) {
  if (Read<uint32_t>(method,offsetof(Method,access_flags))&member_access_abi::kObsoleteMethod) return obsolete_cache(method);
  auto* declaring=read_root(method);
  // Class::GetDexFile/GetDexCache explicitly allow this immutable cache link
  // without a barrier; native DEX and cache array allocation agree in both copies.
  return reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(declaring,offsetof(Class,dex_cache))));
}
const DexFile* Dex(void* method) { return reinterpret_cast<const DexFile*>(Read<uint64_t>(Cache(method),offsetof(DexCache,prefix.dex_file))); }
const char* String(const DexFile* dex,uint32_t index) {
  const auto& map=dex->prefix.mapping;
  const uint32_t count=Read<uint32_t>(dex->prefix.header,56);
  Require(index<count);
  uint32_t offset=Read<uint32_t>(dex->prefix.string_ids,size_t(index)*4);
  for (unsigned n=0;n<5;++n) {
    Require(offset<map.data_size); unsigned byte=map.data_begin[offset++];
    if (!(byte&0x80)) { Require(std::memchr(map.data_begin+offset,0,map.data_size-offset)!=nullptr); return reinterpret_cast<const char*>(map.data_begin+offset); }
  }
  std::abort();
}
bool FixtureClass(void* klass) {
  constexpr const char* fixture="Lcom/github/ytlog/artdcevm/fixture/NarrowParent;";
  const auto flags=Read<uint32_t>(klass,offsetof(Class,access_flags));
  if (flags&member_access_abi::kProxyClass) return proxy_descriptor(klass,fixture);
  if (Read<uint32_t>(klass,offsetof(Class,component_type)) || (Read<uint32_t>(klass,offsetof(Class,primitive_type))&kPrimitiveTypeMask)) return false;
  auto* cache=reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(klass,offsetof(Class,dex_cache))));
  auto* dex=reinterpret_cast<const DexFile*>(Read<uint64_t>(cache,offsetof(DexCache,prefix.dex_file)));
  auto index=static_cast<uint16_t>(Read<int32_t>(klass,offsetof(Class,dex_type_index)));
  Require(index<Read<uint32_t>(dex->prefix.header,64));
  return std::strcmp(String(dex,Read<uint32_t>(dex->prefix.type_ids,size_t(index)*4)),fixture)==0;
}
void Field(void* thread,Handle receiver,void* field) {
  auto* object=receiver.slot ? reinterpret_cast<void*>(uintptr_t(*receiver.slot)) : nullptr;
  if (!object) return;
  if (!IsValidationAssignableFrom(read_root(field),ReadHeapReference(object,0))) {
    invalid_hits.fetch_add(1,std::memory_order_relaxed);
    throw_exception(thread,"Ljava/lang/IncompatibleClassChangeError;","Receiver is not an instance of the field declaring class"); return;
  }
  if (!force_reject || std::strcmp(field_name(field),"ancestor") || !FixtureClass(read_root(field))) return;
  field_hits.fetch_add(1,std::memory_order_relaxed);
  throw_exception(thread,"Ljava/lang/IncompatibleClassChangeError;","Field access probe rejected access before storage mutation");
}
void* FrameReference(void* frame,uint32_t reg) {
  auto regs=Read<uint32_t>(frame,offsetof(member_access_abi::ShadowFrame,registers));
  return reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(frame,sizeof(member_access_abi::ShadowFrame)+size_t(regs+reg)*4)));
}
InstructionView Instructions(void* method) {
  const auto flags=Read<uint32_t>(method,offsetof(Method,access_flags));
  if (Read<uint32_t>(method,offsetof(Method,dex_method_index))==UINT32_MAX || (flags&(0x100|0x400)) ||
      (Read<uint32_t>(read_root(method),offsetof(Class,access_flags))&member_access_abi::kProxyClass)) return {};
  auto* code=reinterpret_cast<const uint16_t*>(Read<uint64_t>(method,offsetof(Method,data))&~uintptr_t{1});
  if (!code) return {};
  const DexFile* dex=Dex(method);
  uint32_t count; const uint16_t* instructions;
  if (dex->compact) {
    const uint16_t bits=code[1]; count=bits>>kCompactCountShift;
    if (bits&kCompactCountPreheader) count+=uint32_t(code[-1])+(uint32_t(code[-2])<<16);
    instructions=code+2;
  } else { count=Read<uint32_t>(code,12); instructions=code+8; }
  return {dex,instructions,count};
}
void DexPc(void* thread,void* method,uint32_t pc) {
  const auto view=Instructions(method);
  const auto* dex=static_cast<const DexFile*>(view.dex);
  const auto* instructions=view.instructions;
  const uint32_t count=view.count;
  if (pc>=count) return;
  const uint16_t* instruction=instructions+pc;
  const uint8_t op=instruction[0]&0xff;
  bool range;
  switch(op) {
    case 0x6e:case 0x6f:case 0x70:case 0x72: range=false;break;
    case 0x74:case 0x75:case 0x76:case 0x78: range=true;break;
    default:return;
  }
  auto* frame=Read<void*>(thread,kManagedStackOffset+offsetof(ManagedStack,shadow));
  if (!frame || Read<void*>(frame,offsetof(member_access_abi::ShadowFrame,method))!=method ||
      Read<uint32_t>(frame,offsetof(member_access_abi::ShadowFrame,pc))!=pc) { frame_misses.fetch_add(1,std::memory_order_relaxed);return; }
  const uint32_t reg=range ? instruction[2] : instruction[2]&0xf;
  if (reg>=Read<uint32_t>(frame,offsetof(member_access_abi::ShadowFrame,registers)) || !FrameReference(frame,reg)) return;
  const uint32_t index=instruction[1];
  if (index>=Read<uint32_t>(dex->prefix.header,88)) return;
  const uint16_t type=Read<MethodId>(dex->prefix.method_ids,size_t(index)*sizeof(MethodId)).class_index;
  auto* cache=Cache(method);
  auto* entries=reinterpret_cast<uint64_t*>(Read<uint64_t>(cache,offsetof(DexCache,types)));
  void* declared=nullptr;
  if (entries) {
    const auto pair=__atomic_load_n(entries+(type%kTypeSlots),__ATOMIC_RELAXED);
    TypePair local; std::memcpy(&local,&pair,sizeof(local));
    if (local.index==type) declared=read_root(&local.object);
  }
  if (!declared) declared=resolve_type(RuntimeClassLinker(),type,method);
  if (!declared) return;
  invoke_checks.fetch_add(1,std::memory_order_relaxed);
  // ResolveType may suspend and move the receiver. Its ShadowFrame root must
  // be reloaded after the call, exactly as the device listener does.
  auto* receiver=FrameReference(frame,reg);
  if (!IsValidationAssignableFrom(declared,ReadHeapReference(receiver,0))) {
    invoke_rejections.fetch_add(1,std::memory_order_relaxed);
    throw_exception(thread,"Ljava/lang/IncompatibleClassChangeError;","Receiver is not an instance of the invoked method declaring class");
  }
}
class OwnedListener final : public Listener {
 public:
  void MethodEntered(void*,void*) override {}
  void MethodExited(void*,void*,OptionalFrame,Handle&) override {}
  void MethodExited(void*,void*,OptionalFrame,uint64_t&) override {}
  void MethodUnwind(void*,Handle,void*,uint32_t) override {}
  void DexPcMoved(void* t,Handle,void* m,uint32_t pc) override { DexPc(t,m,pc); }
  void FieldRead(void* t,Handle h,void*,uint32_t,void* f) override { Field(t,h,f); }
  void FieldWritten(void* t,Handle h,void*,uint32_t,void* f,Handle) override { Field(t,h,f); }
  void FieldWritten(void* t,Handle h,void*,uint32_t,void* f,const uint64_t&) override { Field(t,h,f); }
  void ExceptionThrown(void*,Handle) override {}
  void ExceptionHandled(void*,Handle) override {}
  void Branch(void*,void*,uint32_t,int32_t) override {}
  void WatchedFramePop(void*,const member_access_abi::ShadowFrame&) override {}
} listener;
}
bool SetReceiverGuardMode(bool enable,bool reject) {
  NativeSuspendScope native(current()); Require(native.Supported());
  OwnedSuspendAll suspend("ART DCEVM field read probe");
  if (!enable && ReceiverGuardRequired()) return false;
  if (enable==ReceiverGuardEnabled()) return true;
  auto* instrumentation=RuntimeInstrumentation();
  if (enable) {
    field_hits=0;invalid_hits=0;invoke_checks=0;invoke_rejections=0;frame_misses=0;
    force_reject=reject;add_listener(instrumentation,&listener,kEvents);deopt_all(instrumentation,"artdcevm-field-read-probe");
  } else { remove_listener(instrumentation,&listener,kEvents);undeopt_all(instrumentation,"artdcevm-field-read-probe");force_reject=false; }
  SetReceiverGuardEnabled(enable);return true;
}
uint64_t FieldReadProbeHits() { return field_hits.load(std::memory_order_relaxed); }
uint64_t InvokeGuardDiagnostics(int index) { switch(index) { case 0:return invoke_checks.load(std::memory_order_relaxed);case 1:return invoke_rejections.load(std::memory_order_relaxed);case 2:return frame_misses.load(std::memory_order_relaxed);default:return 0; } }
void SetDeoptTracing(bool enabled) { tracing.store(enabled,std::memory_order_relaxed); }
void TraceDeopt(void*,void* frames,void* result,bool from_code,int type) {
  if (!tracing.load(std::memory_order_relaxed)) return;
  for (int depth=0;frames && depth<64;++depth,frames=Read<void*>(frames)) {
    auto* method=Read<void*>(frames,offsetof(member_access_abi::ShadowFrame,method));
    const uint32_t index=Read<uint32_t>(method,offsetof(Method,dex_method_index));
    if (index==UINT32_MAX) continue;
    auto* dex=Dex(method); auto id=Read<MethodId>(dex->prefix.method_ids,size_t(index)*sizeof(MethodId));
    if (std::strcmp(String(dex,id.name_index),"doFrame")) continue;
#ifdef __ANDROID__
    uint32_t regs=Read<uint32_t>(frames,offsetof(member_access_abi::ShadowFrame,registers));
    __android_log_print(ANDROID_LOG_INFO,"ArtDcevmDeopt","doFrame depth=%d pc=%u regs=%u from_code=%d type=%d result=0x%llx v2=0x%x ref2=%p",depth,Read<uint32_t>(frames,offsetof(member_access_abi::ShadowFrame,pc)),regs,from_code,type,static_cast<unsigned long long>(Read<uint64_t>(result)),regs>2 ? Read<uint32_t>(frames,64+8):0,regs>2 ? FrameReference(frames,2):nullptr);
#else
    (void)result;(void)from_code;(void)type;
#endif
  }
}
bool ConfigureInstrumentationListener(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
#define SYMBOL(variable,name) variable=reinterpret_cast<decltype(variable)>(hooks.FindSymbol("libart.so",name)); if (!variable) { *error="Missing instrumentation symbol: " name;return false; }
  SYMBOL(current,"_ZN3art6Thread14CurrentFromGdbEv")
  SYMBOL(read_root,"artReadBarrierForRootSlow")
  SYMBOL(obsolete_cache,"_ZN3art9ArtMethod19GetObsoleteDexCacheEv")
  SYMBOL(resolve_type,"_ZN3art11ClassLinker13DoResolveTypeIPNS_9ArtMethodEEENS_6ObjPtrINS_6mirror5ClassEEENS_3dex9TypeIndexET_")
  SYMBOL(field_name,"_ZN3art8ArtField7GetNameEv")
  SYMBOL(proxy_descriptor,"_ZN3art6mirror5Class21ProxyDescriptorEqualsEPKc")
  SYMBOL(throw_exception,"_ZN3art6Thread17ThrowNewExceptionEPKcS2_")
  SYMBOL(add_listener,"_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj")
  SYMBOL(remove_listener,"_ZN3art15instrumentation15Instrumentation14RemoveListenerEPNS0_23InstrumentationListenerEj")
  SYMBOL(deopt_all,"_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc")
  SYMBOL(undeopt_all,"_ZN3art15instrumentation15Instrumentation22UndeoptimizeEverythingEPKc")
#undef SYMBOL
  return true;
}
}

#include "platform/android13/selective_deopt.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/frame_recovery.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/code_info_flags_abi.h"
#include "platform/common/modern_frame_service.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include <cstring>
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
void** singleton; void** list_lock;
void (*lock)(void*,void*); void (*unlock)(void*,void*);
void (*for_each)(void*,void (*)(void*,void*),void*);
modern_frame::Service instrument;
void* (*make_context)();
uintptr_t (*return_pc)(const void*); uint8_t* (*flag_addr)(const void*);
void* (*root)(void*);
template<class T> T Read(const void* p,size_t offset) { T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value));return value; }
// arch/context.h defines a virtual destructor before all Context operations.
class Context { public: virtual ~Context()=default; };
struct AppVisitor final:stack_abi::Visitor {
  AppVisitor(void* t,void* c):Visitor(t,c,stack_abi::WalkKind::kIncludeInlined),frames(Read<void*>(t,selective_abi::kThreadInstrumentationStack)) {}
  bool VisitFrame() override {
    void* m=Method();
    if (!m || !quick_frame || Read<uint32_t>(m,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
        (art_method_abi::ReadAccessFlags(m)&0x100)) return true;
    void* klass=root(m);
    if (!klass || !ReadHeapReference(klass,offsetof(validation_abi::ClassPrefix,class_loader))) return true;
    stack_abi::CodeInfo info{};
    // The named device decoder establishes native code ownership. The carrier
    // owns the slot even when the selected application method is an inlinee.
    bool has_flag=ReadEvolutionCodeInfo(Read<void*>(quick_frame,0),header,pc,&info) &&
        code_info_flags_abi::DecodeMatchedCas(info.flags).should_deoptimize.value_or(false) && IsEvolutionFrameRecoverable(*this);
    if (has_flag) {
      uint8_t* flag=flag_addr(this); *flag|=selective_abi::kDebugFlag;
    } else {
      auto* frame=selective_abi::FindFrame(frames,return_pc(this));
      if (!frame) std::abort(); // InstrumentThreadStack must record every app quick frame.
      const uint64_t sentinel=UINT64_MAX;
      std::memcpy(reinterpret_cast<unsigned char*>(frame)+offsetof(selective_abi::Frame,force_deopt_id),&sentinel,sizeof(sentinel));
    }
    return true;
  }
  void* frames;
};
void VisitThread(void* thread,void* instrumentation) {
  if(!instrument.PrepareLegacyStack(instrumentation,thread)) std::abort();
  auto* context=static_cast<Context*>(make_context());
  { AppVisitor visitor(thread,context);visitor.Walk(true); }
  delete context;
}
}
void DeoptimizeApplicationFrames() {
  void* runtime=*singleton;
  void* self=CurrentHierarchyThread();
  void* threads=Read<void*>(runtime,offsetof(runtime_access_abi::RuntimePrefix,thread_list));
  void* instrumentation=static_cast<unsigned char*>(runtime)+offsetof(selective_abi::RuntimePrefix,instrumentation);
  lock(*list_lock,self);
  for_each(threads,VisitThread,instrumentation);
  unlock(*list_lock,self);
}
bool ConfigureSelectiveDeopt(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
#define GET(dst,type,name) dst=reinterpret_cast<type>(hooks.FindSymbol("libart.so",name))
  GET(singleton,void**,"_ZN3art7Runtime9instance_E");
  GET(list_lock,void**,"_ZN3art5Locks17thread_list_lock_E");
  GET(lock,decltype(lock),"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");
  GET(unlock,decltype(unlock),"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
  GET(for_each,decltype(for_each),"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");
  modern_frame::Service candidate;
  bool bound=modern_frame::Service::Bind(modern_frame::Model::kLegacyExitStack,
      hooks.FindSymbol("libart.so",modern_frame::Service::SymbolName(modern_frame::Model::kLegacyExitStack)),&candidate);
  GET(make_context,decltype(make_context),"_ZN3art7Context6CreateEv");
  GET(return_pc,decltype(return_pc),"_ZNK3art12StackVisitor15GetReturnPcAddrEv");
  GET(flag_addr,decltype(flag_addr),"_ZNK3art12StackVisitor27GetShouldDeoptimizeFlagAddrEv");
  GET(root,decltype(root),"artReadBarrierForRootSlow");
#undef GET
  if (!singleton || !*singleton || !list_lock || !lock || !unlock || !for_each || !bound || !make_context || !return_pc || !flag_addr || !root) {
    *error="Missing named application frame deoptimization functions";return false;
  }
  instrument=candidate;
  return true;
}
}

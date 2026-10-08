#include "platform/android13/frame_recovery.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/runtime_access.h"
#include "platform/common/code_item_metadata.h"
#include "platform/common/decoded_frame_recovery.h"
#include <cstring>
#if defined(ARTDCEVM_TESTING) && defined(__ANDROID__)
#include <android/log.h>
#define RECOVERY_LOG(...) __android_log_print(ANDROID_LOG_ERROR,"ArtDcevmRecovery",__VA_ARGS__)
#else
#define RECOVERY_LOG(...) ((void)0)
#endif
namespace artdcevm::android13 {
namespace {
abi::arm64::android13::ConstructCodeInfo construct;
abi::arm64::android13::FindStackMap stack_map;
bool (*nterp)(const void*);
void* (*root)(void*);
void* (*obsolete_cache)(void*);
bool (*contains_pc)(const void*,const void*);
const void* (*oat_code)(void*,size_t);
bool (*oat_debuggable)(const void*);
template<class T> T Read(const void* p,size_t at=0) {T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
void* MethodCache(void* method) {
  auto flags=art_method_abi::ReadAccessFlags(method);
  void* cache=nullptr;
  if(flags&0x40000) cache=obsolete_cache(method);
  else {
    auto* klass=root(method);if(!klass) return nullptr;
    cache=reinterpret_cast<void*>(uintptr_t{Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,dex_cache))});
  }
  return cache;
}
bool Registers(void* method,uint32_t* count) {
  void* cache=MethodCache(method);
  if(!cache) return false;
  auto* dex=reinterpret_cast<void*>(Read<uint64_t>(cache,offsetof(validation_abi::DexCachePrefix,dex_file)));
  if(!dex) return false;
  auto map=Read<validation_abi::DexMappingPrefix>(dex);
  return code_item_metadata::ReadRegisters(code_item_metadata::kTagged,method,sizeof(art_method_abi::ArtMethod),
      {map.begin,map.size,map.data_begin,map.data_size,0},count);
}
struct PcVisitor final:stack_abi::Visitor {
  PcVisitor(void* thread,uintptr_t target):Visitor(thread,nullptr,stack_abi::WalkKind::kIncludeInlined),target(target) {}
  bool VisitFrame() override {
    if(!quick_frame || pc!=target) return true;
    if(carrier && carrier!=quick_frame) return false;
    void* method=Method();if(!method) return true;
    if(Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
       (art_method_abi::ReadAccessFlags(method)&0x100)) return true;
    carrier=quick_frame;found=true;
    if(!IsEvolutionFrameRecoverable(*this)) {safe=false;return false;}
    return true;
  }
  uintptr_t target;void** carrier=nullptr;bool found=false,safe=true;
};
}
bool ReadEvolutionCodeInfo(void* outer,const void* header,uintptr_t pc,stack_abi::CodeInfo* info) {
  if(!construct || !outer || !header || !info || nterp(header)) return false;
  // The device CodeInfo constructor owns vendor header decoding. The locked
  // upstream header bit is not a device capability check. Only compiled code
  // whose native ownership is established may reach that constructor.
  void* jit=RuntimeJitCodeCache();
  bool jit_owned=jit && contains_pc(jit,reinterpret_cast<const void*>(pc));
  if(!jit_owned) {
    const void* code=outer?oat_code(outer,8):nullptr;
    if(!code || header!=static_cast<const unsigned char*>(code)-stack_abi::kCodeOffset)
      return false;
    void* cache=MethodCache(outer);
    const void* dex=cache?reinterpret_cast<void*>(Read<uint64_t>(cache,offsetof(validation_abi::DexCachePrefix,dex_file))):nullptr;
    const void* oat_dex=dex?Read<const void*>(dex,offsetof(stack_abi::DexPrefix,oat_dex)):nullptr;
    const void* oat=oat_dex?Read<const void*>(oat_dex):nullptr;
    if(!oat || !oat_debuggable(oat)) return false;
  }
  construct(info,header);
  return true;
}
bool IsEvolutionFrameRecoverable(const stack_abi::Visitor& visitor) {
  auto reject=[&](const char* reason,uint32_t expected=0,uint32_t available=0) {
    (void)reason;(void)expected;(void)available;
    RECOVERY_LOG("reject=%s thread=%p method=%p header=%p pc=%zx inline=%u:%u expected=%u available=%u",
                 reason,visitor.thread,visitor.Method(),visitor.header,size_t(visitor.pc),
                 visitor.inline_frames.begin.row,visitor.inline_frames.end.row,expected,available);
    RECOVERY_LOG("header-shape header=%p word=%08x",visitor.header,visitor.header?Read<uint32_t>(visitor.header):0);
    return false;
  };
  if(!construct || !visitor.quick_frame || !visitor.header) return reject("missing-frame");
  if(nterp(visitor.header)) return true; // Nterp has direct register arrays, no CodeInfo maps.
  auto entry=reinterpret_cast<uintptr_t>(visitor.header)+stack_abi::kCodeOffset;
  if(visitor.pc<entry) return reject("pc-before-entry");
  void* method=visitor.Method();if(!method) return reject("no-method");
  uint32_t expected;if(!Registers(method,&expected)) {
    RECOVERY_LOG("register-read method=%p data=%zx flags=%x",method,
                 size_t(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,data))),
                 art_method_abi::ReadAccessFlags(method));
    return reject("codeitem");
  }
  stack_abi::CodeInfo info{};
  if(!ReadEvolutionCodeInfo(Read<void*>(visitor.quick_frame),visitor.header,visitor.pc,&info)) return reject("code-owner-or-aot-policy");
  auto map=stack_map(&info,visitor.pc-entry,2); // SOURCE InstructionSet::kArm64.
  if(map.table!=&info.stack_maps || map.row>=info.stack_maps.rows) return reject("no-stackmap",expected);
  code_info_view::View view;
  if(!code_info_view::ReadDecoded(info,&view))return reject("decoded-table-bounds",expected);
  uint32_t index;
  if(!view.StackMaps().Read(map.row,7,&index))return reject("map-bounds",expected);
  uint32_t available;
  if(!decoded_frame_recovery::LegacyCount(view,map.row,visitor.inline_frames.begin.row,
                                   visitor.inline_frames.end.row,&available))
    return reject("register-map-bounds",expected);
  // This matches Get[Inline]DexRegisterMapOf().size() exactly. A missing map
  // cannot be legitimized by Runtime::IsJavaDebuggable or a private JIT address.
  return available==expected || reject(index==UINT32_MAX?"missing-map":"register-count",expected,available);
}
bool IsEvolutionPcRecoverable(void* thread,uintptr_t pc) {
  if(!thread || !construct) return false;
  PcVisitor visitor(thread,pc);visitor.Walk();return visitor.found&&visitor.safe;
}
bool ConfigureFrameRecovery(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()){if(error)*error="Frame recovery requires a matched device profile";return false;}
  auto c=reinterpret_cast<decltype(construct)>(hooks.FindSymbol("libart.so",abi::arm64::android13::kConstructCodeInfo));
  auto m=reinterpret_cast<decltype(stack_map)>(hooks.FindSymbol("libart.so",abi::arm64::android13::kFindStackMap));
  auto n=reinterpret_cast<decltype(nterp)>(hooks.FindSymbol("libart.so","_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv"));
  auto r=reinterpret_cast<decltype(root)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto o=reinterpret_cast<decltype(obsolete_cache)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod19GetObsoleteDexCacheEv"));
  auto p=reinterpret_cast<decltype(contains_pc)>(hooks.FindSymbol("libart.so","_ZNK3art3jit12JitCodeCache10ContainsPcEPKv"));
  auto q=reinterpret_cast<decltype(oat_code)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE"));
  auto d=reinterpret_cast<decltype(oat_debuggable)>(hooks.FindSymbol("libart.so","_ZNK3art7OatFile12IsDebuggableEv"));
  if(!c||!m||!n||!r||!o||!p||!q||!d){if(error)*error="Missing named stack-map recovery services";return false;}
  construct=c;stack_map=m;nterp=n;root=r;obsolete_cache=o;contains_pc=p;oat_code=q;oat_debuggable=d;return true;
}
}

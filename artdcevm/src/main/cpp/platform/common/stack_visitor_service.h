#pragma once
#include "platform/abi/arm64/stack_visitor.h"
#include <cstring>

namespace artdcevm::stack_visitor_service {
enum class Declaration { kAndroid11, kAndroid12, kAndroid13To16, kAndroid17 };
using WalkKind=abi::arm64::stack::WalkKind;

// Caller selects a source declaration only after ELF/profile and owned layout
// calibration. SDK is not an ABI key. The complete object must be a constructed
// compatible C++ visitor with destructor/VisitFrame virtual slots, and remain
// live under ART's required mutator/suspension rules for all native calls.
class Service {
 public:
  static size_t StorageSize(Declaration declaration) {
    switch(declaration) {
      case Declaration::kAndroid11: return sizeof(abi::arm64::stack::Visitor11);
      case Declaration::kAndroid12: return sizeof(abi::arm64::stack::Visitor12);
      case Declaration::kAndroid13To16: return sizeof(abi::arm64::stack::Visitor13);
      case Declaration::kAndroid17: return sizeof(abi::arm64::stack::Visitor17);
    }
    return 0;
  }
  static const char* ConstructSymbol() {return abi::arm64::stack::kConstruct;}
  static const char* DexPcSymbol() {return abi::arm64::stack::kDexPc;}
  static const char* MethodSymbol() {return abi::arm64::stack::kMethod;}
  static const char* WalkSymbol(Declaration declaration) {
    if(!StorageSize(declaration)) return nullptr;
    return declaration==Declaration::kAndroid17 ? abi::arm64::stack::kWalk17 : abi::arm64::stack::kWalk;
  }
  // finder resolves exact names in the already matched loaded ART module.
  // A missing name never tries another source family's ABI. Failure preserves out.
  template<class Finder> static bool Bind(Declaration declaration, Finder finder, Service* output) {
    const size_t size=StorageSize(declaration);
    if(!output || !size) return false;
    Service candidate;
    candidate.construct_=reinterpret_cast<abi::arm64::stack::Construct>(finder(ConstructSymbol()));
    candidate.walk_=reinterpret_cast<abi::arm64::stack::Walk>(finder(WalkSymbol(declaration)));
    candidate.method_=reinterpret_cast<abi::arm64::stack::Method>(finder(MethodSymbol()));
    if(!candidate.construct_ || !candidate.walk_ || !candidate.method_) return false;
    candidate.size_=size;
    *output=candidate;
    return true;
  }
  // Optional named capability: existing walkers do not silently require it.
  // Failed bindings retain the old capability; no ABI/family fallback exists.
  template<class Finder> bool BindDexPc(Finder finder) {
    if(!size_)return false;
    auto candidate=reinterpret_cast<abi::arm64::stack::DexPc>(finder(DexPcSymbol()));
    if(!candidate)return false;dex_pc_=candidate;return true;
  }
  bool DexPc(const void*visitor,size_t extent,uint32_t*out)const {
    if(!dex_pc_||!out||!Valid(visitor,extent))return false;
    *out=dex_pc_(visitor,false);return true;
  }
  // These are common source-declared prefix fields, never a ShadowFrame layout.
  bool ShadowHead(const void*visitor,size_t extent,void**thread,void**shadow)const {
    if(!thread||!shadow||!Valid(visitor,extent))return false;
    void* quick=nullptr;void* actual_thread=nullptr;void* actual_shadow=nullptr;
    const auto*bytes=static_cast<const unsigned char*>(visitor);
    std::memcpy(&actual_thread,bytes+abi::arm64::stack::kThreadOffset,sizeof(void*));std::memcpy(&actual_shadow,bytes+abi::arm64::stack::kShadowOffset,sizeof(void*));std::memcpy(&quick,bytes+abi::arm64::stack::kQuickOffset,sizeof(void*));
    if(!actual_thread||!actual_shadow||quick)return false;
    *thread=actual_thread;*shadow=actual_shadow;return true;
  }
  // ART's base constructor temporarily installs its vptr while initializing the
  // complete native cache. Restore the caller vptr before any callback can run.
  // Derived construction then installs the derived vptr as usual.
  bool Construct(void* visitor,size_t extent,void* thread,void* context,WalkKind kind,
                 bool check_suspended=true) const {
    if(!construct_ || !Valid(visitor,extent) || !thread ||
       (kind!=WalkKind::kIncludeInlined && kind!=WalkKind::kSkipInlined)) return false;
    void* own_vptr;
    std::memcpy(&own_vptr,visitor,sizeof(own_vptr));
    if(!own_vptr) return false;
    construct_(visitor,thread,context,kind,check_suspended);
    std::memcpy(visitor,&own_vptr,sizeof(own_vptr));
    return true;
  }
  bool Walk(void* visitor,size_t extent,bool transitions=false) const {
    if(!walk_ || !Valid(visitor,extent)) return false;
    walk_(visitor,transitions);
    return true;
  }
  // A null method is a valid ART result at a transition. Invalid calls preserve out.
  bool Method(const void* visitor,size_t extent,void** output) const {
    if(!method_ || !output || !Valid(visitor,extent)) return false;
    *output=method_(visitor);
    return true;
  }
 private:
  bool Valid(const void* visitor,size_t extent) const {
    const auto address=reinterpret_cast<uintptr_t>(visitor);
    return size_ && visitor && address%8==0 && extent>=size_ && address<=UINTPTR_MAX-size_;
  }
  size_t size_=0;
  abi::arm64::stack::Construct construct_=nullptr;
  abi::arm64::stack::Walk walk_=nullptr;
  abi::arm64::stack::Method method_=nullptr;
  abi::arm64::stack::DexPc dex_pc_=nullptr;
};
}

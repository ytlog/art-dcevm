#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::handle_root_abi {
// Locked Android 13 runtime/thread.h public constexpr offsets, evaluated with
// the repository's release arm64 source profile and statically checked in probe.
inline constexpr size_t kTopHandleScopeOffset = 280;
inline constexpr size_t kExceptionOffset = 152;
#pragma pack(push, 4)
struct ScopeHeader { void* link; int32_t count; };
#pragma pack(pop)
inline constexpr size_t kReferencesOffset = 12;
static_assert(sizeof(ScopeHeader)==12 && alignof(ScopeHeader)==4);
// runtime/gc_root.h: foreign virtual RootVisitor destructor precedes both
// VisitRoots overloads. RootInfo is a borrowed native vptr/type/thread-id view.
struct RootInfo { const void* vptr; uint32_t type,thread_id; };
struct RootVisitor {
 virtual ~RootVisitor()=default;
 virtual void VisitRoots(void***,size_t,const RootInfo&)=0;
 virtual void VisitRoots(uint32_t**,size_t,const RootInfo&)=0;
};
static_assert(sizeof(RootInfo)==16 && offsetof(RootInfo,type)==8);
static_assert(sizeof(RootVisitor)==8);
inline constexpr char kVisitRoots[]="_ZN3art6Thread21HandleScopeVisitRootsEPNS_11RootVisitorEj";
inline constexpr char kPeer[]="_ZNK3art6Thread22GetPeerFromOtherThreadEv";
inline constexpr char kCurrentThread[] = "_ZN3art6Thread14CurrentFromGdbEv";
}

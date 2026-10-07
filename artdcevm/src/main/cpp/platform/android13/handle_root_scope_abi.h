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
inline constexpr char kCurrentThread[] = "_ZN3art6Thread14CurrentFromGdbEv";
}

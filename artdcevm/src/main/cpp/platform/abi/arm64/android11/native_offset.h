#pragma once
#include <cstddef>
#include <type_traits>
namespace artdcevm::abi::arm64::android11 {
// Fixed Offset and data-less MemberOffset subclass have one size_t value.
// Preserve the full native word; Int32Value is an accessor, not return ABI.
struct NativeOffset {size_t value;};
using SelfOffset=NativeOffset (*)(size_t);
inline constexpr const char* kSelfOffset="_ZN3art9JNIEnvExt10SelfOffsetEm";
static_assert(sizeof(NativeOffset)==8&&alignof(NativeOffset)==8&&std::is_trivially_copyable_v<NativeOffset>);
}

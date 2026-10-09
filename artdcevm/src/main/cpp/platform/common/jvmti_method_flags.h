#pragma once
#include <cstdint>
namespace artdcevm::jvmti_method_flags {
// Fixed ART MethodUtil::GetMethodModifiers / Executable.fixMethodFlags.
// This projects a raw ArtMethod, not a DEX declaration or public modifiers.
// Fields and Classes do not share these method-only runtime transformations.
constexpr uint32_t Public(uint32_t raw)noexcept {
 if(raw&0x400u)raw&=~0x100u;  // Abstract methods hide ART's native dispatch bit.
 raw&=~0x20u;               // Public synchronized comes from declared-sync.
 if(raw&0x20000u)raw|=0x20u;
 return raw&0xffffu;
}
}

#pragma once

// android-16.0.0_r1 runtime/verifier/reg_type_cache.{h,cc}: precision was
// removed and FromClass uses the cache's own loader and DEX. ART 17 agrees.
namespace artdcevm::abi::arm64::android16::verifier {
using FromClass = const void* (*)(void*, void*);
}

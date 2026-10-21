#pragma once
#include <string_view>

// android-13.0.0_r1 runtime/verifier/reg_type_cache.{h,cc}; the same
// signatures exist in the fixed ART 11--15 sources. ObjPtr<Class> is one
// arm64 pointer; RegType results are opaque references, never dereferenced.
namespace artdcevm::abi::arm64::android13::verifier {
using FindClass = const void* (*)(const void*, void*, bool);
using InsertClass = const void* (*)(void*, const std::string_view&, void*, bool);
using GetComponentType = const void* (*)(void*, const void*, void*);
using AssignableFrom = bool (*)(const void*, const void*, bool, void*);
using From = const void* (*)(void*, void*, const char*, bool);
using FromClass = const void* (*)(void*, const char*, void*, bool);
using Construct = void (*)(void*, void*, bool, void*, bool);
}

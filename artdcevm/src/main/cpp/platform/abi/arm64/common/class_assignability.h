#pragma once
#include <cstddef>
namespace artdcevm::abi::arm64::android_common {
using ClassAssignableObjPtr=bool(*)(void* target,void* source);
// Fixed AOSP ed6c006bd06ae060bd9698fd2cb25c4865512ec3,
// runtime/entrypoints/quick/quick_cast_entrypoints.cc:21. C linkage, size_t
// result, not bool/ObjPtr ABI. Both arguments are actual mirror::Class*.
using ClassAssignableCode=size_t(*)(void* target,void* source);
inline constexpr char kClassAssignableObjPtr[]="_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE";
inline constexpr char kClassAssignableCode[]="artIsAssignableFromCode";
}

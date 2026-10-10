#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android_common {
// First five Redefiner members in the fixed Android 11--17 ti_redefine.h
// declarations. Borrowed source storage; never construct a platform STL member.
struct RedefinerPrefix {
 void* environment;int32_t result;uint32_t padding;
 void* runtime;void* self;int32_t type;
};
static_assert(offsetof(RedefinerPrefix,result)==8&&offsetof(RedefinerPrefix,runtime)==16&&
              offsetof(RedefinerPrefix,self)==24&&offsetof(RedefinerPrefix,type)==32);
}

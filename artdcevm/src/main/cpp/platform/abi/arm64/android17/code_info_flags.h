#pragma once
#include "platform/common/code_info_flag_schema.h"
namespace artdcevm::code_info_flags_abi::android17 {
// Android 17 fixed release runtime/oat/stack_map.h adds fast compilation.
// Declares scalar masks only, never the CodeInfo object layout.
inline constexpr code_info_flags::Schema kSchema{1, 4, 2, 8, 16};
}

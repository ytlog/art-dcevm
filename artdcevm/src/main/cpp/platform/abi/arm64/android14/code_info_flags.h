#pragma once
#include "platform/common/code_info_flag_schema.h"
namespace artdcevm::code_info_flags_abi::android14 {
// Android 14 fixed release stack_map.h; deopt flag still lives in the oat header.
// Declares scalar masks only, never the CodeInfo object layout.
inline constexpr code_info_flags::Schema kSchema{1, 2, 0, 4, 0};
}

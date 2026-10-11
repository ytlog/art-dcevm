#pragma once
#include "platform/common/native_call_graph.h"
#include "platform/common/native_receiver_bytecode.h"
#include <vector>
namespace artdcevm::native_call_graph {
struct CallSite {
 uint32_t dex_pc=0;
 receiver_guard::InvokeInstruction instruction{};
};
InvokeKind Kind(receiver_guard::InvokeKind)noexcept;
// Ordinary preparation only. Enumerates all encoded invoke instructions,
// including unreachable instructions and exception handlers. Payload data and
// operands are never call sites. This does not resolve dispatch or certify the
// completeness of a loaded-method inventory, and grants no frame authority.
// On any failure clears the output so an earlier successful scan cannot escape.
bool ScanCode(const receiver_guard::CodeView&,std::vector<CallSite>*,const char** failure=nullptr);
}

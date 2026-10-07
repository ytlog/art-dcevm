#include "platform/android13/final_field_guard_state.h"

namespace artdcevm::android13 {
namespace {
FinalFieldGuardState state;
}

void MarkFinalFieldGuard() noexcept { state.Mark(); }
bool FinalFieldGuardEnabled() noexcept { return state.Enabled(); }

}  // namespace artdcevm::android13

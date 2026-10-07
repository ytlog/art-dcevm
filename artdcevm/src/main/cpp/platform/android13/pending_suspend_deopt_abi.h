#pragma once
#include <cstdint>

namespace artdcevm::android13 {

using MarkPendingSuspendDeopt = bool (*)(void*);
using IsPendingSuspendDeopt = bool (*)(void*);
using CancelPendingSuspendDeopt = void (*)(void*);
using ResetPendingSuspendDeopt = void (*)();
using SuspendDeoptHits = uint64_t (*)();
using ConfigurePendingSuspendDeopt = void (*)(MarkPendingSuspendDeopt,
    IsPendingSuspendDeopt, CancelPendingSuspendDeopt, ResetPendingSuspendDeopt,
    SuspendDeoptHits);

}  // namespace artdcevm::android13

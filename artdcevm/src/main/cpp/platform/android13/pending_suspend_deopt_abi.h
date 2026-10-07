#pragma once

namespace artdcevm::android13 {

using MarkPendingSuspendDeopt = bool (*)(void*);
using IsPendingSuspendDeopt = bool (*)(void*);
using CancelPendingSuspendDeopt = void (*)(void*);
using ResetPendingSuspendDeopt = void (*)();
using ConfigurePendingSuspendDeopt = void (*)(MarkPendingSuspendDeopt,
    IsPendingSuspendDeopt, CancelPendingSuspendDeopt, ResetPendingSuspendDeopt);

}  // namespace artdcevm::android13

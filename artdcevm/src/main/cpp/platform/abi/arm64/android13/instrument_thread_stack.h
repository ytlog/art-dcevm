#pragma once

namespace artdcevm::abi::arm64::android13 {
// Fixed instrumentation.h declaration shared by Android 13–17. The native
// function signature is shared, but its frame model changes in Android 14.
inline constexpr char kInstrumentThreadStack[] =
    "_ZN3art15instrumentation15Instrumentation21InstrumentThreadStackEPNS_6ThreadEb";
using InstrumentThreadStack = void (*)(void* instrumentation, void* thread, bool force_deopt);
}

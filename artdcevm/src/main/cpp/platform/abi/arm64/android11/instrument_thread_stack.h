#pragma once

namespace artdcevm::abi::arm64::android11 {
// Fixed instrumentation.h declaration shared by Android 11–12.1.
inline constexpr char kInstrumentThreadStack[] =
    "_ZN3art15instrumentation15Instrumentation21InstrumentThreadStackEPNS_6ThreadE";
using InstrumentThreadStack = void (*)(void* instrumentation, void* thread);
}

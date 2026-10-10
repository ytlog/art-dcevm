#pragma once
#include <cstdint>
#include <type_traits>
namespace artdcevm::abi::arm64::android_common {
// Fixed ART b753cf97923c3695338d21466fa14c57b480a59a runtime/instrumentation.h:
// scoped enum has the default int underlying type. Also exported by the actual
// Pixel 9 Android 14 ART ELF (independently inventoried named capability).
// This declaration is selected by its named overload, never the OS version.
enum class ListenerType {kRegularListener,kFastTraceListener,kSlowTraceListener};
static_assert(std::is_same_v<std::underlying_type_t<ListenerType>,int>);
static_assert(sizeof(ListenerType)==4);
inline constexpr char kTypedAddListener[]=
    "_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjNS1_12ListenerTypeE";
using TypedAddListener=void (*)(void*,void*,uint32_t,ListenerType);
}

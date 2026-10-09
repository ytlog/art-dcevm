#include "platform/arm32/jni_receiver_calibration.h"
static_assert(sizeof(void*)==4);
// Instantiate the actual ARM32 native JNI callback/return ABI under NDK, not a
// host-width emulation. There is no full-private capability token here.
template class artdcevm::jni_receiver_calibration::Service<4>;

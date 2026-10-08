#pragma once
#include "platform/common/method_array_view.h"
#include "platform/common/method_identity_calibration.h"
#include <string>
namespace artdcevm::android17 {
// Fixed android-17.0.0_r1 b753cf97923c mirror/class.h declaration. Every
// declared method (constructors included) must have an independent APK DEX
// index and Java reflection oracle. The caller roots Class and all reflected
// members and holds a calibrated Runnable scope throughout this operation.
// This validates table storage and identity, not ArtMethod data/entry points.
bool CalibrateMethodTable(const runnable_jni::Context&,
    abi::arm64::android_common::FromReflectedMethod named_leaf,
    const method_identity::Calibration& identity_model, const void* rooted_class,
    const evolution::MappedDexMemberCounts& dex_counts,
    const method_identity::Oracle* methods, size_t count,
    method_array::View* output, std::string* error);
}  // namespace artdcevm::android17

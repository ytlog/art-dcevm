#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_receiver_source_calibration.h"
#include "platform/common/native_reference_decoder.h"
#include "platform/common/native_source_stop.h"
namespace artdcevm::production_bootstrap {
// Ordinary Native preparation, actual original Runnable execution and scoped
// native-set activation/refund of ten unchanged isolated caller methods.
// No schema, graph drain, metadata disposal or transaction authority is granted.
bool CalibrateReceiverInvocations(HookEngine&,JNIEnv*,const MetadataCalibration&,
 const source_handle_roots::Service&,const native_source_stop::Service&,
 native_reference_decoder::Decoder&,receiver_guard::SourceCalibration&,std::string*);
}

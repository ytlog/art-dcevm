#pragma once
#include "platform/common/production_metadata_calibration.h"
#include "platform/common/native_final_write_guard.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
namespace artdcevm::production_bootstrap {
// All four exact real bytecode writer/storage episodes, with native flags and
// owned interpretation entries restored before returning. No transaction proof.
bool CalibrateFinalWrites(HookEngine&,JNIEnv*,const MetadataCalibration&,
 const source_handle_roots::Service&,const native_source_stop::Service&,
 final_write_guard::Guard&,std::string*);
}

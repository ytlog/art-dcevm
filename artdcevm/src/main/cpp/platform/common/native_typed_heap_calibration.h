#pragma once
#include "platform/common/native_typed_heap_walk.h"
namespace artdcevm::native_typed_heap_calibration {
// Existing CalibrationFixture instance; all temporary strong globals are owned
// here. Heap/current must come from independent actual runtime/source binding.
bool Prepare(HookEngine&,JNIEnv*,jobject fixture,void* observed_heap,void*(*named_current)(),
 const modern_field_metadata::FieldMetadata&,const exclusive_owned_roots::Service&,
 native_typed_heap_walk::Service*,std::string*);
}

#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <string>
#include <vector>
namespace artdcevm::modern_field_metadata {class FieldMetadata;}
namespace artdcevm::native_interface_graph {
// Fixed IfTable : ObjectArray<Object>, pair {interface, method-array}. All
// managed references pass the existing calibrated ART read barrier.
bool Read(const modern_field_metadata::FieldMetadata&,void* klass,std::vector<void*>*,std::string*);
bool Matches(const modern_field_metadata::FieldMetadata&,void* klass,const std::vector<void*>&,std::string*);
// Public Class.getInterfaces/getSuperclass independently establish the complete
// closure, including inherited and transitive interfaces, inside ordinary JNI.
// A positive ArrayList oracle and the real field fixture must both match.
// No borrowed managed Class or JNI root survives this call.
bool Calibrate(const runnable_jni::Context&,jobject fixture,const modern_field_metadata::FieldMetadata&,std::string*);
}

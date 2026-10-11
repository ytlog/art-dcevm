#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <string>
#include <cstddef>
#include <vector>
namespace artdcevm::modern_field_metadata {class FieldMetadata;struct ReadContext;}
namespace artdcevm::native_interface_graph {
// Production supplies its already calibrated native strong-root decoder.
// Register before borrowing any values; read invokes no JNI or safepoint.
struct StrongRoots {
 void*owner=nullptr;
 bool(*register_root)(void*,JNIEnv*,jobject,std::string*)=nullptr;
 void(*forget)(void*,jobject)=nullptr;
 bool(*read)(void*,void*actual_thread,jobject,void**)=nullptr;
};
// Ephemeral, non-owning native view for a single actual no-safepoint read
// interval. It is not a root or publication permission and cannot survive GC.
class View final {
 public:
 size_t Count()const noexcept{return count_;}
 private:
 friend bool ReadView(const modern_field_metadata::FieldMetadata&,void*,View*,std::string*,const modern_field_metadata::ReadContext*);
 friend bool InterfaceAt(const modern_field_metadata::FieldMetadata&,const View&,size_t,void**,const modern_field_metadata::ReadContext*);
 void*klass_=nullptr,*table_=nullptr,*meta_class_=nullptr,*array_class_=nullptr;size_t count_=0;
};
bool ReadView(const modern_field_metadata::FieldMetadata&,void* klass,View*,std::string*,const modern_field_metadata::ReadContext* =nullptr);
bool InterfaceAt(const modern_field_metadata::FieldMetadata&,const View&,size_t,void**,const modern_field_metadata::ReadContext* =nullptr);
// Fixed IfTable : ObjectArray<Object>, pair {interface, method-array}. All
// managed references pass the existing calibrated ART read barrier.
bool Read(const modern_field_metadata::FieldMetadata&,void* klass,std::vector<void*>*,std::string*);
bool Matches(const modern_field_metadata::FieldMetadata&,void* klass,const std::vector<void*>&,std::string*);
bool ReadInContext(const modern_field_metadata::FieldMetadata&,void* klass,std::vector<void*>*,std::string*,const modern_field_metadata::ReadContext*);
bool MatchesInContext(const modern_field_metadata::FieldMetadata&,void* klass,const std::vector<void*>&,std::string*,const modern_field_metadata::ReadContext*);
// Public Class.getInterfaces/getSuperclass independently establish the complete
// closure, including inherited and transitive interfaces, inside ordinary JNI.
// A positive ArrayList oracle and the real field fixture must both match.
// No borrowed managed Class or JNI root survives this call.
bool Calibrate(const runnable_jni::Context&,jobject fixture,const modern_field_metadata::FieldMetadata&,std::string*,const StrongRoots* =nullptr);
}

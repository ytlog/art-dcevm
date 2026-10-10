#pragma once
#include "api/hook_engine.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/native_field_factory_observer.h"
#include "platform/common/source_handle_roots.h"
#include <vector>
#include <string>
namespace artdcevm::native_lazy_field_mirror {
// Only the independently matched release ObjPtr carrier may use these source
// declarations. A symbol inventory alone never grants factory readiness.
struct Bindings {
 void* owner=nullptr;
 // Candidate prerequisite from completed Runnable/FieldMetadata/full public
 // Field mirror calibration. It never grants Ready; literal true is invalid.
 bool matched_release_carrier=false;
 bool (*scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 void* (*decode_field)(void*,const runnable_jni::Context&,void* declaring,jfieldID)=nullptr;
 void* (*field)(void*,void* mirror)=nullptr;
 void* (*declaring)(void*,void* native_field)=nullptr;
 // Same process's independently validated original public episodes. These are
 // observations, not symbol-presence permission or borrowed trace slots.
 const native_field_factory_observer::Service* observer=nullptr;
 const native_field_factory_observer::Trace* primitive_trace=nullptr;
 const native_field_factory_observer::Trace* optional_trace=nullptr;
 const std::array<bool,4>* public_roots=nullptr;
 source_handle_roots::Service* roots=nullptr;
};
struct Oracle {jobject strong_field=nullptr;jclass exact_owner=nullptr;jfieldID id=nullptr;void* native_field=nullptr;bool is_static=false;};
class Service {
 public:
  Service()=default;
  Service(const Service&)=delete;
  Service& operator=(const Service&)=delete;
  bool Bind(HookEngine&,const Bindings&,std::string*);
  bool Calibrate(JNIEnv*,const std::vector<Oracle>&,std::string*);
  // Output is one caller-owned JNI global, not a borrowed managed pointer.
  bool Create(JNIEnv*,jclass,jfieldID,bool is_static,jobject* strong_field,std::string*);
  bool Ready()const noexcept{return calibrated_;}
  void Clear(JNIEnv*);
 private:
  struct Request;
  static bool Produce(const runnable_jni::Context&,void*);
  bool ProducePublic(JNIEnv*,jclass,jfieldID,bool,void* expected,jobject*,std::string*);
  Bindings bindings_{};
  using Factory=uintptr_t (*)(void*,void*,bool);
  using Local=jobject (*)(JNIEnv*,void*);
  void* factory_=nullptr;Local local_=nullptr;bool four_=false;
  jobject field_class_=nullptr;
  bool FourProven()const;
  jobject carrier_=nullptr;bool bound_=false,calibrated_=false;
};
}

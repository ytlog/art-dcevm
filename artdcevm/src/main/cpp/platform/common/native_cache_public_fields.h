#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include "platform/common/modern_field_metadata.h"
#include <jvmti.h>
#include <memory>
namespace artdcevm::native_cache_public_fields {
// Read-only public/native identity observation. It grants no cache layout,
// storage extent, write permission or capability. Use in ordinary Native JNI.
class Witness {
 public:
  ~Witness();
  Witness(const Witness&)=delete;
  Witness& operator=(const Witness&)=delete;
  jfieldID Id()const{return id_;}
  jlong Value()const{return value_;}
  void* NativeField()const{return native_;}
  // All three references are strong, owned by this witness, and remain live
  // until its destructor on the creating JNI thread. Never use after release.
  jobject Cache()const{return cache_;}
  jclass DeclaringClass()const{return owner_;}
  jobject ReflectedField()const{return reflected_;}
 private:
  friend class Service;
  explicit Witness(JNIEnv* env):env_(env){}
  JNIEnv* env_;jobject cache_=nullptr; jclass owner_=nullptr;
  jobject reflected_=nullptr;jfieldID id_=nullptr;jlong value_=0;void* native_=nullptr;
};
class Service {
 public:
  // manager must come from the independent shared Runtime receiver snapshot.
  // Binding named leaves does not establish availability or a storage permit.
  bool Bind(HookEngine&,const runtime_services::Snapshot&,std::string*);
  // Exact bootstrap DexCache Class, exact object Class, declared instance J
  // field and original-ID reflection roundtrip. Native identity is observed in
  // the established Runnable executor, never a fabricated JNI/mutator scope.
  // Calibrated metadata independently checks the native field's name/J/owner.
  // Pending exceptions stop each leaf and are preserved. Output unchanged on failure.
  bool Observe(JNIEnv*,jvmtiEnv*,jobject,jclass,const char* field_name,
      const modern_field_metadata::FieldMetadata&,std::unique_ptr<Witness>*,std::string*)const;
 private:
  using Field=void*(*)(void*);using Decode=void*(*)(void*,jfieldID);
  Field field_=nullptr;Decode decode_=nullptr;void* manager_=nullptr;
};
}

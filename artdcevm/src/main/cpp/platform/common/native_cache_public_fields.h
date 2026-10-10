#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include "platform/common/modern_field_metadata.h"
#include <jvmti.h>
#include "platform/common/source_handle_roots.h"
#include "platform/common/contextual_field_id.h"
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
  jobject reflected_=nullptr;std::shared_ptr<contextual_field_id::Proof> identity_;jfieldID id_=nullptr;jlong value_=0;void* native_=nullptr;std::string name_;
};
class Service {
 public:
  // Refresh inside the actual original source Runnable owner. Its bound roots
  // service proves current Thread/JNIEnv/VM; executor TLS is not this owner.
  // Never fall back to a second bridge or decode path on an identity refusal.
  bool Refresh(const runnable_jni::Context&,const source_handle_roots::Service&,const Witness&,
      const modern_field_metadata::FieldMetadata&,jlong*,std::string*)const;
  static void Release(JNIEnv*,std::unique_ptr<Witness>*);
  // Uses the shared public contextual Field identity owner; no manager ID cast.
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
  using Field=void*(*)(void*);
  Field field_=nullptr;
  // Fixed openjdkjvmti ti_class/ti_field declarations. Diagnostic only: these
  // original source leaves never supply a storage grant when live API refuses.
  using Fields=jvmtiError(*)(jvmtiEnv*,jclass,jint*,jfieldID**);
  using Name=jvmtiError(*)(jvmtiEnv*,jclass,jfieldID,char**,char**,char**);
  using Flags=jvmtiError(*)(jvmtiEnv*,jclass,jfieldID,jint*);
  Fields original_fields_=nullptr;Name original_name_=nullptr;Flags original_flags_=nullptr;
};
}

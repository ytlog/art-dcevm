#pragma once
#include "api/hook_engine.h"
#include "platform/abi/common/jni_id_query_scope.h"
#include <string>
#include <cstdint>
namespace artdcevm::canonical_jni_members {
// Distinct typed canonical capabilities. An opaque public JNI ID can only enter
// through the named decoder; these wrappers never cast it to a native address.
struct Method {void* address=nullptr;};
struct Field {void* address=nullptr;};
class Scope {
 public:
  struct Declarations {
    abi::common::JniIdQueryConstruct construct=nullptr;
    abi::common::JniIdQueryDestroy destroy=nullptr;
    abi::common::DecodeMethodId method=nullptr;
    abi::common::DecodeFieldId field=nullptr;
    static Declarations Bind(HookEngine& hooks) {
      return {reinterpret_cast<abi::common::JniIdQueryConstruct>(hooks.FindSymbol("libart.so",abi::common::kJniIdQueryConstruct)),
              reinterpret_cast<abi::common::JniIdQueryDestroy>(hooks.FindSymbol("libart.so",abi::common::kJniIdQueryDestroy)),
              reinterpret_cast<abi::common::DecodeMethodId>(hooks.FindSymbol("libart.so",abi::common::kDecodeMethodId)),
              reinterpret_cast<abi::common::DecodeFieldId>(hooks.FindSymbol("libart.so",abi::common::kDecodeFieldId))};
    }
  };
  // ONLY from an independently calibrated live Runnable callback. Original
  // ctor/dtor observe and balance the real manager. All decoded handles and
  // the scope remain borrowed; no manager/native pointer may outlive its roots.
  Scope(const Declarations& declarations):declarations_(declarations) {
    if(declarations_.construct&&declarations_.destroy&&declarations_.method&&declarations_.field){
      declarations_.construct(&scope_);active_=true;
    }
  }
  ~Scope(){if(active_)declarations_.destroy(&scope_);}
  Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
  bool Ready()const{return active_&&scope_.manager&&!failed_;}
  bool Verify(jmethodID id,Method independently_observed,std::string* error) {
    if(!Ready()||!id||!independently_observed.address||declarations_.method(scope_.manager,id)!=independently_observed.address){
      failed_=true;if(error)*error="Opaque JNI Method ID disagrees with independent original reflected leaf";return false;
    }
    ++methods_;distinct_methods_+=reinterpret_cast<uintptr_t>(id)!=reinterpret_cast<uintptr_t>(independently_observed.address);return true;
  }
  bool Read(jfieldID id,Field* output,std::string* error) {
    if(!Ready()||!methods_||!id||!output){if(error)*error="Field ID lacks independently proven JNI manager";return false;}
    void* field=declarations_.field(scope_.manager,id);
    if(!field){if(error)*error="Original JNI field decoder refused ID";return false;}
    *output={field};++fields_;distinct_fields_+=reinterpret_cast<uintptr_t>(id)!=reinterpret_cast<uintptr_t>(field);return true;
  }
  size_t Methods()const{return methods_;}size_t Fields()const{return fields_;}
  size_t DistinctMethods()const{return distinct_methods_;}size_t DistinctFields()const{return distinct_fields_;}
 private:
  Declarations declarations_;abi::common::JniIdQueryScope scope_{};
  bool active_=false,failed_=false;size_t methods_=0,fields_=0,distinct_methods_=0,distinct_fields_=0;
};
}

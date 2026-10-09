#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <jni.h>
#include <string>
#include <vector>
#include <unordered_map>
namespace artdcevm::rooted_field_identities {
// Public jfieldID remains opaque and contextual to its exact declaring Class.
// No Manager DecodeFieldId ABI is assumed. Staging is ordinary JNI; all later
// reads use only live strong roots and the calibrated named Field leaf.
struct Bindings {
 void* owner=nullptr;
 bool (*scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 void* (*field)(void*,void*)=nullptr;
 void* (*declaring)(void*,void*)=nullptr;
};
class Store {
 public:
  explicit Store(Bindings bindings):bindings_(bindings){}
  bool Stage(JNIEnv*,jclass,jfieldID,bool is_static,std::string*);
  void* Read(jclass,jfieldID)const noexcept;
  void Clear(JNIEnv*);
 private:
  struct Entry {jclass owner=nullptr;jobject mirror=nullptr;jfieldID id=nullptr;void* native=nullptr;bool is_static=false;};
  struct Request;
  static bool Observe(const runnable_jni::Context&,void*);
  Bindings bindings_;std::unordered_map<jfieldID,std::vector<Entry>> entries_;
};
}

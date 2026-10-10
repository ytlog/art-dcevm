#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <jni.h>
#include <string>
#include <vector>
#include <unordered_map>
namespace artdcevm::rooted_field_identities {
// Public jfieldID stays opaque and contextual to its declaring Class. Mirrors
// are strongly held by shared Field[] roots, not one JNI global per Field.
struct Bindings {
 void* owner=nullptr;
 bool (*scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 void* (*field)(void*,void*)=nullptr;
 void* (*declaring)(void*,void*)=nullptr;
 // Original calibrated heap-reference barrier at fixed ObjectArray slot.
 // Staging proves EVERY slot against its public array/mirror identity.
 void* (*element)(void*,void* array,size_t index)=nullptr;
 // Calibrated original source lazy factory. One new caller-owned strong Field
 // root is returned; no borrowed managed result crosses public JNI.
 bool (*lazy_mirror)(void*,JNIEnv*,jclass,jfieldID,bool,jobject*,std::string*)=nullptr;
};
class Store {
 public:
  static constexpr jsize kChunkSize=1024;
  explicit Store(Bindings bindings):bindings_(bindings){}
  bool Stage(JNIEnv*,jclass,jfieldID,bool is_static,std::string*);
  void* Read(jclass,jfieldID)const noexcept;
  void* ReadNative(void* owner,jfieldID)const noexcept;
  void Clear(JNIEnv*);
 private:
  struct Entry {jfieldID id=nullptr;void* native=nullptr;bool is_static=false;size_t chunk=0;jsize slot=0;};
  struct Chunk {jobjectArray root=nullptr;jsize used=0;};
  struct Request;
  static bool Observe(const runnable_jni::Context&,void*);
  Bindings bindings_;std::unordered_map<jfieldID,std::vector<Entry>> entries_;
  std::vector<Chunk> chunks_;
};
}

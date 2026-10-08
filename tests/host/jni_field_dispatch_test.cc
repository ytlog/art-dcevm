#include "platform/common/jni_field_dispatch.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
using namespace artdcevm::jni_field;
namespace {
JNIEnv* expected_env=reinterpret_cast<JNIEnv*>(0x1000);
jfieldID removed=reinterpret_cast<jfieldID>(0x2000), kept=reinterpret_cast<jfieldID>(0x3000);
int originals=0, guards=0, exception=0;
bool GuardField(JNIEnv* env,jfieldID id,jobject mirror) {
  assert(env==expected_env&&!mirror);++guards;
  if(exception)return true;
  if(id==removed){exception=1;return true;}
  assert(id==kept);return false;
}
jobject contextual_receiver=nullptr;bool contextual_static=false;
bool GuardContextual(JNIEnv* env,jobject receiver,bool stat,jfieldID id){assert(receiver==contextual_receiver&&stat==contextual_static);return GuardField(env,id,nullptr);}
template<class T,class R> struct Access {
  static inline R receiver=nullptr;
  static inline T value{};
  static T Get(JNIEnv* env,R actual,jfieldID id){
    assert(env==expected_env&&actual==receiver&&id==kept);++originals;return value;
  }
  static void Set(JNIEnv* env,R actual,jfieldID id,T actual_value){
    assert(env==expected_env&&actual==receiver&&id==kept);++originals;value=actual_value;
  }
};
template<class T,class R> void Check(T value) {
  auto& receiver=Access<T,R>::receiver;receiver=reinterpret_cast<R>(0x4000);
  Access<T,R>::value=value;exception=0;int before=originals;
  assert((Get<T,R>(expected_env,receiver,kept,GuardField,Access<T,R>::Get)==value));
  Set<T,R>(expected_env,receiver,kept,value,GuardField,Access<T,R>::Set);
  assert((originals==before+2&&Access<T,R>::value==value));
  // The simulated CheckJNI originals deliberately assert on a removed ID:
  // interception must happen before ownership validation, not afterwards.
  assert((Get<T,R>(expected_env,receiver,removed,GuardField,Access<T,R>::Get)==T{}));
  assert(exception==1&&originals==before+2);exception=0;
  Set<T,R>(expected_env,receiver,removed,value,GuardField,Access<T,R>::Set);
  assert(exception==1&&originals==before+2);
  exception=7;
  assert((Get<T,R>(expected_env,receiver,kept,GuardField,Access<T,R>::Get)==T{}));
  Set<T,R>(expected_env,receiver,kept,value,GuardField,Access<T,R>::Set);
  assert(exception==7&&originals==before+2);
}
template<class T,class R> void Contextual(T value){
 Access<T,R>::receiver=reinterpret_cast<R>(0x6000);contextual_receiver=Access<T,R>::receiver;contextual_static=std::is_same_v<R,jclass>;
 Access<T,R>::value=value;exception=0;int before=originals;
 assert((GetContextual<T,R>(expected_env,Access<T,R>::receiver,kept,GuardContextual,Access<T,R>::Get)==value));
 SetContextual<T,R>(expected_env,Access<T,R>::receiver,kept,value,GuardContextual,Access<T,R>::Set);assert(originals==before+2);
 assert((GetContextual<T,R>(expected_env,Access<T,R>::receiver,removed,GuardContextual,Access<T,R>::Get)==T{}));exception=0;
 SetContextual<T,R>(expected_env,Access<T,R>::receiver,removed,value,GuardContextual,Access<T,R>::Set);assert(originals==before+2);
 exception=7;assert((GetContextual<T,R>(expected_env,Access<T,R>::receiver,kept,GuardContextual,Access<T,R>::Get)==T{}));
 SetContextual<T,R>(expected_env,Access<T,R>::receiver,kept,value,GuardContextual,Access<T,R>::Set);assert(exception==7&&originals==before+2);
}
template<class T> void Both(T value){Check<T,jobject>(value);Check<T,jclass>(value);Contextual<T,jobject>(value);Contextual<T,jclass>(value);}
}
int main(){
  Both<jboolean>(1);Both<jbyte>(-122);Both<jchar>(65530);Both<jshort>(-32000);
  Both<jint>(-1234567);Both<jlong>(0x123456789abcdefLL);Both<jfloat>(-3.25f);
  Both<jdouble>(1.234567891);Both<jobject>(reinterpret_cast<jobject>(0x5000));
  assert(originals==72&&guards==216);
  std::puts("JNI field dispatch: 18 instance/static typed pairs preserve arguments, retired IDs precede CheckJNI validation, pending exceptions preserved");
}

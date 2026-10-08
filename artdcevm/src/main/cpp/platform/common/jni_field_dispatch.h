#pragma once
#include <jni.h>
#include <type_traits>
namespace artdcevm::jni_field {
// check_jni.cc FIELD_ACCESSORS has the public JNI ABI. Intercept before its
// class/field ownership checks, which otherwise abort on obsolete declaring
// roots. All surviving IDs still reach ART's complete original validation.

using ContextualGuard=bool (*)(JNIEnv*,jobject,bool,jfieldID);
template<class T,class Receiver>
T GetContextual(JNIEnv* env,Receiver receiver,jfieldID id,ContextualGuard guard,
      T (*original)(JNIEnv*,Receiver,jfieldID)) {
 if(guard(env,receiver,std::is_same_v<Receiver,jclass>,id))return T{};
 return original(env,receiver,id);
}
template<class T,class Receiver>
void SetContextual(JNIEnv* env,Receiver receiver,jfieldID id,T value,ContextualGuard guard,
      void (*original)(JNIEnv*,Receiver,jfieldID,T)) {
 if(guard(env,receiver,std::is_same_v<Receiver,jclass>,id))return;
 original(env,receiver,id,value);
}
using Guard = bool (*)(JNIEnv*,jfieldID,jobject);
template<class T,class Receiver>
T Get(JNIEnv* env,Receiver receiver,jfieldID id,Guard guard,
      T (*original)(JNIEnv*,Receiver,jfieldID)) {
  if(guard(env,id,nullptr))return T{};
  return original(env,receiver,id);
}
template<class T,class Receiver>
void Set(JNIEnv* env,Receiver receiver,jfieldID id,T value,Guard guard,
         void (*original)(JNIEnv*,Receiver,jfieldID,T)) {
  if(guard(env,id,nullptr))return;
  original(env,receiver,id,value);
}
}

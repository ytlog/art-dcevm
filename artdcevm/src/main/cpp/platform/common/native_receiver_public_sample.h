#pragma once
#include <jni.h>
#include <jvmti.h>
#include <initializer_list>
namespace artdcevm::receiver_guard {
// Ordinary JNI only. Every public leaf is checked before another JNI query;
// destructors perform only reference/buffer cleanup, including refusal paths.
inline jobject AcquireGlobal(JNIEnv*env,jobject local){
 if(!env||!local||env->ExceptionCheck())return nullptr;
 jobject root=env->NewGlobalRef(local);
 if(env->ExceptionCheck()){if(root)env->DeleteGlobalRef(root);return nullptr;}
 return root;
}
struct PublicInputs final {
 explicit PublicInputs(JNIEnv*e):env(e){}
 PublicInputs(const PublicInputs&)=delete;
 ~PublicInputs(){for(jobject v:{jobject(type),object,jobject(callers),jobject(targets),jobject(bootstrap)})if(v)env->DeleteLocalRef(v);}
 bool Read(jobject input){
  if(!env||!input||env->ExceptionCheck())return false;
  type=env->GetObjectClass(input);if(env->ExceptionCheck()||!type)return false;
  auto object_id=env->GetFieldID(type,"receiverSourceObject","Ljava/lang/Object;");if(env->ExceptionCheck()||!object_id)return false;
  auto callers_id=env->GetFieldID(type,"receiverCallers","[Ljava/lang/reflect/Method;");if(env->ExceptionCheck()||!callers_id)return false;
  auto targets_id=env->GetFieldID(type,"receiverTargets","[Ljava/lang/reflect/Method;");if(env->ExceptionCheck()||!targets_id)return false;
  object=env->GetObjectField(input,object_id);if(env->ExceptionCheck()||!object)return false;
  callers=static_cast<jobjectArray>(env->GetObjectField(input,callers_id));if(env->ExceptionCheck()||!callers)return false;
  targets=static_cast<jobjectArray>(env->GetObjectField(input,targets_id));if(env->ExceptionCheck()||!targets)return false;
  auto count=env->GetArrayLength(callers);if(env->ExceptionCheck()||count!=10)return false;
  count=env->GetArrayLength(targets);if(env->ExceptionCheck()||count!=10)return false;
  bootstrap=env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap");if(env->ExceptionCheck()||!bootstrap)return false;
  run=env->GetStaticMethodID(bootstrap,"runReceiverSource","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)I");return !env->ExceptionCheck()&&run;
 }
 JNIEnv*env;jclass type=nullptr,bootstrap=nullptr;jobject object=nullptr;jobjectArray callers=nullptr,targets=nullptr;jmethodID run=nullptr;
};
struct PublicSample final {
 PublicSample(JNIEnv*e,jvmtiEnv*t):env(e),ti(t){}
 PublicSample(const PublicSample&)=delete;
 ~PublicSample(){if(public_bytes)ti->Deallocate(public_bytes);if(caller)env->DeleteLocalRef(caller);if(target)env->DeleteLocalRef(target);}
 bool Read(jobjectArray callers,jobjectArray targets,jint index){
  if(!env||!ti||!callers||!targets||index<0||index>=10||env->ExceptionCheck())return false;
  caller=env->GetObjectArrayElement(callers,index);if(env->ExceptionCheck()||!caller)return false;
  target=env->GetObjectArrayElement(targets,index);if(env->ExceptionCheck()||!target)return false;
  caller_id=env->FromReflectedMethod(caller);if(env->ExceptionCheck()||!caller_id)return false;
  target_id=env->FromReflectedMethod(target);if(env->ExceptionCheck()||!target_id)return false;
  if(ti->GetMethodModifiers(caller_id,&caller_flags)!=JVMTI_ERROR_NONE||env->ExceptionCheck())return false;
  if(ti->GetMethodModifiers(target_id,&target_flags)!=JVMTI_ERROR_NONE||env->ExceptionCheck())return false;
  const auto result=ti->GetBytecodes(caller_id,&bytes,&public_bytes);
  return result==JVMTI_ERROR_NONE&&!env->ExceptionCheck()&&bytes>0&&public_bytes;
 }
 JNIEnv*env;jvmtiEnv*ti;jobject caller=nullptr,target=nullptr;jmethodID caller_id=nullptr,target_id=nullptr;
 jint caller_flags=0,target_flags=0,bytes=0;unsigned char*public_bytes=nullptr;
};
}

namespace artdcevm::receiver_guard {
struct PublicClassSample final {
 explicit PublicClassSample(JNIEnv*e):env(e){}
 PublicClassSample(const PublicClassSample&)=delete;
 ~PublicClassSample(){for(jobject v:{jobject(type),parent,loader,jobject(interfaces),jobject(object_type)})if(v)env->DeleteLocalRef(v);}
 bool Read(jclass holder){
  if(!env||!holder||env->ExceptionCheck())return false;
  type=env->GetObjectClass(holder);if(env->ExceptionCheck()||!type)return false;
  auto parent_id=env->GetMethodID(type,"getSuperclass","()Ljava/lang/Class;");if(env->ExceptionCheck()||!parent_id)return false;
  auto interfaces_id=env->GetMethodID(type,"getInterfaces","()[Ljava/lang/Class;");if(env->ExceptionCheck()||!interfaces_id)return false;
  auto loader_id=env->GetMethodID(type,"getClassLoader","()Ljava/lang/ClassLoader;");if(env->ExceptionCheck()||!loader_id)return false;
  auto modifiers_id=env->GetMethodID(type,"getModifiers","()I");if(env->ExceptionCheck()||!modifiers_id)return false;
  modifiers=env->CallIntMethod(holder,modifiers_id);if(env->ExceptionCheck())return false;
  parent=env->CallObjectMethod(holder,parent_id);if(env->ExceptionCheck())return false;
  loader=env->CallObjectMethod(holder,loader_id);if(env->ExceptionCheck())return false;
  interfaces=static_cast<jobjectArray>(env->CallObjectMethod(holder,interfaces_id));if(env->ExceptionCheck()||!interfaces)return false;
  count=env->GetArrayLength(interfaces);if(env->ExceptionCheck()||count<0)return false;
  if(modifiers&0x200){if(parent)return false;object_type=env->FindClass("java/lang/Object");if(env->ExceptionCheck()||!object_type)return false;}
  return true;
 }
 JNIEnv*env;jclass type=nullptr,object_type=nullptr;jobject parent=nullptr,loader=nullptr;jobjectArray interfaces=nullptr;jint modifiers=0;jsize count=0;
};
}

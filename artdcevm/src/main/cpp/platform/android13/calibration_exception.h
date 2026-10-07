#pragma once
#include <cstring>
#include <initializer_list>
#include <jni.h>
namespace artdcevm::android13 {
// Initialization only: inspect and consume the exception raised by an owned probe.
inline bool ConsumeCalibrationException(JNIEnv* env,jclass type,jmethodID get_message,
                                        std::initializer_list<const char*> fragments) {
  auto exception=env->ExceptionOccurred();
  if(!exception)return false;
  env->ExceptionClear();
  bool valid=env->IsInstanceOf(exception,type);
  auto message=valid?static_cast<jstring>(env->CallObjectMethod(exception,get_message)):nullptr;
  const char* text=message?env->GetStringUTFChars(message,nullptr):nullptr;
  valid=valid && text;
  if(text)for(const auto* fragment:fragments)valid=valid && std::strstr(text,fragment);
  if(text)env->ReleaseStringUTFChars(message,text);
  if(message)env->DeleteLocalRef(message);
  env->DeleteLocalRef(exception);
  return valid && !env->ExceptionCheck();
}
}

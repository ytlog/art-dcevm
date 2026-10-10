#pragma once
#include <jni.h>
#include <string>
namespace artdcevm::compiled_frame_fixture {
// Ordinary JNI only. The original pending Throwable remains rooted and is
// rethrown unchanged even when public diagnostic methods themselves throw.
inline bool DescribePending(JNIEnv*env,const char*stage,std::string*error)noexcept{
 if(!env||!env->ExceptionCheck())return false;
 jthrowable original=env->ExceptionOccurred();if(!original)return false;
 env->ExceptionClear();
 struct Restore{JNIEnv*env;jthrowable original;~Restore(){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(original);env->DeleteLocalRef(original);}}restore{env,original};
 try{
  std::string text=stage?stage:"compiled-fixture";
  jclass class_type=env->FindClass("java/lang/Class");
  jclass throwable_type=!env->ExceptionCheck()?env->FindClass("java/lang/Throwable"):nullptr;
  if(!class_type||!throwable_type||env->ExceptionCheck()){if(class_type)env->DeleteLocalRef(class_type);if(throwable_type)env->DeleteLocalRef(throwable_type);if(error)*error=text+": Throwable details unavailable";return true;}
  struct Locals{JNIEnv*env;jclass a,b;~Locals(){env->DeleteLocalRef(a);env->DeleteLocalRef(b);}}locals{env,class_type,throwable_type};
  auto name=env->GetMethodID(class_type,"getName","()Ljava/lang/String;");
  auto message=!env->ExceptionCheck()?env->GetMethodID(throwable_type,"getMessage","()Ljava/lang/String;"):nullptr;
  auto cause=!env->ExceptionCheck()?env->GetMethodID(throwable_type,"getCause","()Ljava/lang/Throwable;"):nullptr;
  auto append=[&](jstring string){if(!string||env->ExceptionCheck())return;
   const char*utf=env->GetStringUTFChars(string,nullptr);
   struct Utf{JNIEnv*env;jstring string;const char*chars;~Utf(){if(chars)env->ReleaseStringUTFChars(string,chars);env->DeleteLocalRef(string);}}owned{env,string,utf};
   if(utf){size_t n=0;while(n<2048&&utf[n])++n;const bool truncated=n==2048;while(truncated&&n&&(static_cast<unsigned char>(utf[n])&0xc0)==0x80)--n;text.append(utf,n);if(truncated)text+="...";}
  };
  jthrowable item=original;
  for(unsigned depth=0;depth<4&&item&&!env->ExceptionCheck()&&name&&message&&cause;++depth){
   text+=depth?"; caused by ":": ";auto type=env->GetObjectClass(item);
   if(type&&!env->ExceptionCheck()){append(static_cast<jstring>(env->CallObjectMethod(type,name)));env->DeleteLocalRef(type);}
   if(!env->ExceptionCheck()){text+=": ";append(static_cast<jstring>(env->CallObjectMethod(item,message)));}
   auto next=!env->ExceptionCheck()?static_cast<jthrowable>(env->CallObjectMethod(item,cause)):nullptr;
   const bool repeat=next&&!env->ExceptionCheck()&&env->IsSameObject(next,item);
   if(item!=original)env->DeleteLocalRef(item);
   if(repeat){env->DeleteLocalRef(next);item=nullptr;break;}item=next;
  }
  if(item&&item!=original)env->DeleteLocalRef(item);
  if(env->ExceptionCheck())text+=" [diagnostic accessor threw]";
  if(error)*error=std::move(text);
 }catch(...){/* Diagnostic allocation cannot replace the original Throwable. */}
 return true;
}
}

#pragma once
#include "platform/common/native_jni_exception_calibration.h"
// Models the owned Java body at the original JNI leaf boundary. All private
// registration callbacks are the actual production Service functions.
namespace boot_upcall_java {
int klass,throwable,token,method;const JNINativeMethod*registered=nullptr;
jthrowable pending=nullptr;size_t globals=0,catch_receipts=0;int fault=0;
jboolean JNICALL Check(JNIEnv*){return pending!=nullptr;}
jthrowable JNICALL Occurred(JNIEnv*){return pending;}
void JNICALL LocalDelete(JNIEnv*,jobject){}
jint JNICALL VM(JNIEnv*,JavaVM**out){assert(!pending);*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
jclass JNICALL Find(JNIEnv*,const char*name){assert(!pending);return reinterpret_cast<jclass>(std::strstr(name,"JniException")?&klass:&throwable);}
jobject JNICALL Global(JNIEnv*,jobject value){assert(!pending);++globals;return value;}
void JNICALL DeleteGlobal(JNIEnv*,jobject){assert(globals);--globals;}
jmethodID JNICALL Method(JNIEnv*,jclass,const char*,const char*){assert(!pending);return reinterpret_cast<jmethodID>(&method);}
jobject JNICALL New(JNIEnv*,jclass,jmethodID,const jvalue*){assert(!pending);return reinterpret_cast<jobject>(&token);}
jboolean JNICALL Same(JNIEnv*,jobject a,jobject b){assert(!pending);return a==b;}
jint JNICALL Register(JNIEnv*,jclass,const JNINativeMethod*entries,jint count){assert(count==4&&!registered);registered=entries;return JNI_OK;}
jint JNICALL Unregister(JNIEnv*,jclass){assert(!pending&&registered);registered=nullptr;return JNI_OK;}
void JNICALL Thrower(JNIEnv*,jclass,jmethodID,const jvalue*argument){assert(!pending);pending=static_cast<jthrowable>(argument->l);}
jboolean JNICALL Run(JNIEnv*env,jclass clazz,jmethodID,const jvalue*arg){
 if(fault)return JNI_TRUE; // Public true cannot fabricate the private receipt.
 using Prepare=jlong(*)(JNIEnv*,jclass,jthrowable);using Invoke=void(*)(JNIEnv*,jclass,jlong);using Complete=jboolean(*)(JNIEnv*,jclass,jlong,jthrowable);
 const auto request=reinterpret_cast<Prepare>(registered[0].fnPtr)(env,clazz,static_cast<jthrowable>(arg->l));assert(request);
 reinterpret_cast<Invoke>(registered[1].fnPtr)(env,clazz,request);assert(pending==reinterpret_cast<jthrowable>(&token));
 auto caught=pending;pending=nullptr; // Original Java catch, not native ExceptionClear.
 const auto receipt=reinterpret_cast<Complete>(registered[2].fnPtr)(env,clazz,request,caught);assert(receipt==JNI_TRUE);++catch_receipts;return receipt;
}
void Calibrate(native_jni_exception_calibration::Service&service,JNIEnv*env,std::string*error){
 const auto receipts_before=catch_receipts;assert(!globals&&!registered&&!pending);auto saved=table;table.ExceptionCheck=Check;table.ExceptionOccurred=Occurred;table.DeleteLocalRef=LocalDelete;table.GetJavaVM=VM;
 table.FindClass=Find;table.NewGlobalRef=Global;table.DeleteGlobalRef=DeleteGlobal;table.GetStaticMethodID=Method;table.GetMethodID=Method;
 table.NewObjectA=New;table.IsSameObject=Same;table.RegisterNatives=Register;table.UnregisterNatives=Unregister;
 table.CallStaticVoidMethodA=Thrower;table.CallStaticBooleanMethodA=Run;
 native_jni_exception_calibration::Service fabricated;fault=1;assert(!fabricated.Calibrate(env,error)&&!fabricated.Calibrated());fault=0;
 assert(service.Calibrate(env,error)&&service.MatchesVm(reinterpret_cast<JavaVM*>(&runtime))&&catch_receipts==receipts_before+1&&!globals&&!registered&&!pending);
 table=saved;
}
}

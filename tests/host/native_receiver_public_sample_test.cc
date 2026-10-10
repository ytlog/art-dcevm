#include "platform/common/native_receiver_public_sample.h"
#include "platform/common/native_receiver_resolution_source.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace artdcevm::receiver_guard;
namespace {
JNINativeInterface_ jni{};jvmtiInterface_1_ jvmti{};JNIEnv env{&jni};jvmtiEnv ti{&jvmti};
int calls=0,fail_at=0,locals=0,globals=0,buffers=0;bool pending=false,null_failure=false,tool_error=false,interface_class=false;
jobject object=reinterpret_cast<jobject>(uintptr_t(0x1234));jmethodID method=reinterpret_cast<jmethodID>(uintptr_t(0x4321));
bool Query(){assert(!pending);++calls;bool fail=calls==fail_at;if(fail&&!null_failure&&!tool_error)pending=true;return fail&&null_failure;}
jboolean Exception(JNIEnv*){return pending;}
jclass Class(JNIEnv*,jobject){if(Query())return nullptr;++locals;return reinterpret_cast<jclass>(object);}
jclass Find(JNIEnv*,const char*){return Class(nullptr,nullptr);}
jfieldID Field(JNIEnv*,jclass,const char*,const char*){return Query()?nullptr:reinterpret_cast<jfieldID>(method);}
jobject Object(JNIEnv*,jobject,jfieldID){if(Query())return nullptr;++locals;return object;}
jsize Length(JNIEnv*,jarray){return Query()?0:10;}
jmethodID Method(JNIEnv*,jclass,const char*,const char*){return Query()?nullptr:method;}
jobject Element(JNIEnv*,jobjectArray,jsize){if(Query())return nullptr;++locals;return object;}
jmethodID Reflected(JNIEnv*,jobject){return Query()?nullptr:method;}
jmethodID ClassMethod(JNIEnv*,jclass,const char*name,const char*){if(Query())return nullptr;return reinterpret_cast<jmethodID>(uintptr_t(!std::strcmp(name,"getSuperclass")?1:!std::strcmp(name,"getInterfaces")?2:!std::strcmp(name,"getClassLoader")?3:4));}
jint ClassModifiers(JNIEnv*,jobject,jmethodID,va_list){Query();return interface_class?0x200:0;}
jobject ClassObject(JNIEnv*,jobject,jmethodID id,va_list){if(Query())return nullptr;if(interface_class&&uintptr_t(id)==1)return nullptr;++locals;return object;}

jobject Global(JNIEnv*,jobject){if(Query())return nullptr;++globals;return object;}
void DeleteLocal(JNIEnv*,jobject value){assert(value);--locals;}
void DeleteGlobal(JNIEnv*,jobject value){assert(value);--globals;}
jvmtiError Modifiers(jvmtiEnv*,jmethodID,jint*out){Query();*out=1;return calls==fail_at&&tool_error?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Bytes(jvmtiEnv*,jmethodID,jint*size,unsigned char**out){Query();*size=null_failure&&calls==fail_at?0:4;*out=static_cast<unsigned char*>(std::malloc(4));++buffers;return calls==fail_at&&tool_error?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Deallocate(jvmtiEnv*,unsigned char*value){assert(value);--buffers;std::free(value);return JVMTI_ERROR_NONE;}
bool bytecode_cap=false,grant=true;int capability_reads=0,capability_adds=0;
jvmtiError capability_read_error=JVMTI_ERROR_NONE,capability_add_error=JVMTI_ERROR_NONE;
jvmtiError Capabilities(jvmtiEnv*,jvmtiCapabilities*out){++capability_reads;*out={};out->can_get_bytecodes=bytecode_cap;return capability_read_error;}
jvmtiError AddCapabilities(jvmtiEnv*,const jvmtiCapabilities*in){++capability_adds;assert(in->can_get_bytecodes&&!in->can_redefine_classes&&!in->can_retransform_classes);if(grant&&capability_add_error==JVMTI_ERROR_NONE)bytecode_cap=true;return capability_add_error;}
void Reset(int fail=0,bool null=false,bool tool=false){assert(!locals&&!globals&&!buffers);calls=0;fail_at=fail;pending=false;null_failure=null;tool_error=tool;}
}
int main(){
 for(unsigned mask=0;mask<64;++mask){
  ResolutionSource source=ResolutionSource::CheckedMethod;const char* error=nullptr;
  const bool checked=mask&1,type=mask&2,member=mask&4,nterp=mask&8,vm=mask&16,checked_current=mask&32;
  const bool success=SelectResolutionSource(checked,type,member,nterp,vm,&source,&error,checked_current);
  assert(success==(checked||(checked_current&&vm)||(!checked_current&&type&&member&&nterp&&vm)));
  if(success)assert(source==(checked?ResolutionSource::CheckedMethod:checked_current?ResolutionSource::CheckedMethodCurrent:ResolutionSource::Nterp));
  else assert(source==ResolutionSource::CheckedMethod&&error&&*error);
 }

 jni.ExceptionCheck=Exception;jni.GetObjectClass=Class;jni.FindClass=Find;jni.GetFieldID=Field;jni.GetObjectField=Object;jni.GetArrayLength=Length;jni.GetStaticMethodID=Method;jni.GetObjectArrayElement=Element;jni.FromReflectedMethod=Reflected;jni.NewGlobalRef=Global;jni.DeleteLocalRef=DeleteLocal;jni.DeleteGlobalRef=DeleteGlobal;
 jvmti.GetCapabilities=Capabilities;jvmti.AddCapabilities=AddCapabilities;
 assert(PreparePublicBytecodes(nullptr)==JVMTI_ERROR_INVALID_ENVIRONMENT);
 assert(PreparePublicBytecodes(&ti)==JVMTI_ERROR_NONE&&bytecode_cap&&capability_reads==2&&capability_adds==1);
 assert(PreparePublicBytecodes(&ti)==JVMTI_ERROR_NONE&&capability_reads==3&&capability_adds==1);
 bytecode_cap=false;grant=false;assert(PreparePublicBytecodes(&ti)==JVMTI_ERROR_MUST_POSSESS_CAPABILITY);
 capability_add_error=JVMTI_ERROR_NOT_AVAILABLE;assert(PreparePublicBytecodes(&ti)==JVMTI_ERROR_NOT_AVAILABLE);
 capability_read_error=JVMTI_ERROR_INTERNAL;const int before=capability_adds;
 assert(PreparePublicBytecodes(&ti)==JVMTI_ERROR_INTERNAL&&capability_adds==before);
 capability_read_error=JVMTI_ERROR_NONE;capability_add_error=JVMTI_ERROR_NONE;grant=true;
 jvmti.GetMethodModifiers=Modifiers;jvmti.GetBytecodes=Bytes;jvmti.Deallocate=Deallocate;
 Reset();{PublicInputs input(&env);assert(input.Read(object));assert(calls==11);}assert(!locals);
 for(int i=1;i<=11;++i)for(bool null:{false,true}){Reset(i,null);{PublicInputs input(&env);assert(!input.Read(object));assert(calls==i);}assert(!locals);}
 Reset();{PublicSample sample(&env,&ti);assert(sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0));assert(calls==7);}assert(!locals&&!buffers);
 for(int i=1;i<=7;++i){Reset(i);{PublicSample sample(&env,&ti);assert(!sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0));assert(calls==i);}assert(!locals&&!buffers);}
 for(int i=1;i<=4;++i){Reset(i,true);{PublicSample sample(&env,&ti);assert(!sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0));assert(calls==i);}assert(!locals);}
 for(int i=5;i<=7;++i){Reset(i,false,true);{PublicSample sample(&env,&ti);assert(!sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0));assert(calls==i);}assert(!locals&&!buffers);}
 Reset(7,true);{PublicSample sample(&env,&ti);assert(!sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0));assert(calls==7);}assert(!buffers);
 Reset(1);assert(!AcquireGlobal(&env,object)&&!globals&&calls==1&&pending);assert(!AcquireGlobal(&env,object)&&calls==1);
 Reset(1,true);assert(!AcquireGlobal(&env,object)&&!globals&&calls==1);
 Reset();auto root=AcquireGlobal(&env,object);assert(root&&globals==1&&!pending);env.DeleteGlobalRef(root);assert(!globals);
 jni.GetMethodID=ClassMethod;jni.CallIntMethodV=ClassModifiers;jni.CallObjectMethodV=ClassObject;
 for(bool iface:{false,true}){
  interface_class=iface;const int total=iface?11:10;
  Reset();{PublicClassSample sample(&env);assert(sample.Read(reinterpret_cast<jclass>(object))&&calls==total&&bool(sample.modifiers&0x200)==iface);}assert(!locals);
  for(int i=1;i<=total;++i){Reset(i);{PublicClassSample sample(&env);assert(!sample.Read(reinterpret_cast<jclass>(object))&&calls==i);}assert(!locals);}
  for(int i:{1,2,3,4,5,9}){Reset(i,true);{PublicClassSample sample(&env);assert(!sample.Read(reinterpret_cast<jclass>(object))&&calls==i);}assert(!locals);}
 }
 Reset();pending=true;{PublicInputs input(&env);PublicSample sample(&env,&ti);assert(!input.Read(object)&&!sample.Read(reinterpret_cast<jobjectArray>(object),reinterpret_cast<jobjectArray>(object),0)&&!AcquireGlobal(&env,object)&&calls==0);}
 std::puts("receiver public JNI samples: success, every pending/null leaf and error-returned buffer cleanup PASS");
}

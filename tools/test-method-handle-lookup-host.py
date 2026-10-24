#!/usr/bin/env python3
"""Exercise the real JNI lookup guard with public JNI/JVMTI service doubles."""
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/method_handle_lookup.h"
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/android11/method_lookup.h"
#include "platform/abi/arm64/android15/method_lookup.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace artdcevm;
int mode, installs, calls, access_throws, internal_throws, missing_throws, allocations, frees, local_reads, unhooks;
bool bridge_method=false;
bool inherited_target=false, declaring_boot=false, missing_selected=false;
bool lookup_static=false, stack_failure=false, malformed_chain=false, nonpublic=false;
int recursive_levels=0;
jmethodID public_recursive=reinterpret_cast<jmethodID>(23), interface_recursive=reinterpret_cast<jmethodID>(24);
int test_api=36;
extern "C" int android_get_device_api_level(){return test_api;}
bool install_failure, pending, static_method=true, interface_class=true, scope=true, missing_local=false;
bool type_valid=true, name_failure=false, boot=false, obsolete=false, public_class=true;
int requested_kind=0;
const char* requested_name="value";
const char* member_signature="()LResult;";
jobject reflected=reinterpret_cast<jobject>(42), type=reinterpret_cast<jobject>(55);
jclass owner=reinterpret_cast<jclass>(40), requested=reinterpret_cast<jclass>(70);
jstring name=reinterpret_cast<jstring>(99);
jobjectArray arguments=reinterpret_cast<jobjectArray>(56);
std::vector<jclass> parameters, actual_parameters;
jclass actual_return=reinterpret_cast<jclass>(70);
jobject actual_method=reinterpret_cast<jobject>(72);
jobjectArray actual_arguments=reinterpret_cast<jobjectArray>(73);
jmethodID reflected_return=reinterpret_cast<jmethodID>(17), reflected_parameters=reinterpret_cast<jmethodID>(18);
jmethodID instance=reinterpret_cast<jmethodID>(11), virtual_method=reinterpret_cast<jmethodID>(12);
jmethodID declared_method=reinterpret_cast<jmethodID>(20), class_get_method=reinterpret_cast<jmethodID>(21), static_lookup=reinterpret_cast<jmethodID>(22);
jfieldID return_field=reinterpret_cast<jfieldID>(13), flags_field=reinterpret_cast<jfieldID>(15);
void* replacement;
jvmtiEnv* test_env;
void Require(bool v,const char* why) { if(!v) throw std::runtime_error(why); }
char* Copy(const char* s) { allocations++; return strdup(s); }
jvmtiError Deallocate(jvmtiEnv*, unsigned char* p) { if(p) {free(p);frees++;}return JVMTI_ERROR_NONE; }
jobject Original(JNIEnv*,jobject,jstring,jobjectArray) {calls++;return reflected;}
artdcevm::abi::arm64::ArtMethodPrefix resolved_method{};
void* DecodeGlobal(JavaVM* vm,jobject value){Require(vm==reinterpret_cast<JavaVM*>(88)&&value==owner,"wrong global decode");return owner;}
artdcevm::abi::arm64::ArtMethodPrefix* FindInterface(void* klass,std::string_view n,std::string_view signature,size_t size){
 Require(klass==owner&&size==8,"wrong named resolver ABI");
 if(n!=requested_name||signature!=member_signature)return nullptr;
 resolved_method.declaring_class=40;resolved_method.access_flags.store(static_method?9:1);return &resolved_method;
}
jobject NativeLocal(JNIEnv*,void* value){Require(value==owner||value==reinterpret_cast<void*>(41),"wrong nonmoving declaring class");return static_cast<jobject>(value);}
artdcevm::abi::arm64::ArtMethodPrefix* FindClass(void* c,std::string_view n,std::string_view s,size_t size){
 auto* method=FindInterface(c,n,s,size);if(method&&inherited_target)method->declaring_class=41;return method;
}
artdcevm::abi::arm64::ArtMethodPrefix* FindClassLegacy(void* c,std::string_view n,std::string_view s,artdcevm::abi::arm64::android11::PointerSize size){return FindClass(c,n,s,static_cast<size_t>(size));}
artdcevm::abi::arm64::ArtMethodPrefix* FindClassModern(void* c,std::string_view n,std::string_view s,artdcevm::abi::arm64::android15::PointerSize size){return FindClass(c,n,s,static_cast<size_t>(size));}
artdcevm::abi::arm64::ArtMethodPrefix* FindLegacy(void* c,std::string_view n,std::string_view s,artdcevm::abi::arm64::android11::PointerSize size){return FindInterface(c,n,s,static_cast<size_t>(size));}
artdcevm::abi::arm64::ArtMethodPrefix* FindModern(void* c,std::string_view n,std::string_view s,artdcevm::abi::arm64::android15::PointerSize size){return FindInterface(c,n,s,static_cast<size_t>(size));}
jint VM(JNIEnv*,JavaVM** out){*out=reinterpret_cast<JavaVM*>(88);return JNI_OK;}
extern "C" void* shadowhook_dlopen(const char*) {return reinterpret_cast<void*>(1);}
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*,const char* n) {
  std::string symbol=n;
  if(mode==1) return nullptr;
  if(symbol.find("FindClassMethod")!=std::string::npos)return mode==13?nullptr:test_api>=35?HookAddress(FindClassModern):HookAddress(FindClassLegacy);
  if(symbol.find("FindInterfaceMethod")!=std::string::npos)return mode==8?nullptr:test_api>=35?HookAddress(FindModern):HookAddress(FindLegacy);
  if(symbol.find("DecodeGlobal")!=std::string::npos)return HookAddress(DecodeGlobal);
  if(symbol.find("NewLocalRef")!=std::string::npos)return HookAddress(NativeLocal);
  if((mode==2)!=(symbol.find(".__uniq.")!=std::string::npos)) return nullptr;
  if(symbol.find("Class_getDeclaredMethodInternal")!=std::string::npos)return HookAddress(Original);
  return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
extern "C" void* shadowhook_hook_sym_addr(void* t,void* r,void** o){installs++;if(install_failure)return nullptr;replacement=r;*o=t;return t;}
extern "C" int shadowhook_unhook(void*) {unhooks++;return mode==5?1:0;}
jclass Find(JNIEnv*,const char* n){return reinterpret_cast<jclass>(std::strstr(n,"NoSuchMethodException")?8:std::strstr(n,"InternalError")?7:std::strstr(n,"MethodType")?3:std::strstr(n,"Lookup")?2:std::strstr(n,"Exception")?4:1);}
jobject Global(JNIEnv*,jobject o){return o;}
void Delete(JNIEnv*,jobject){}
jboolean Exception(JNIEnv*){return pending;}
void Clear(JNIEnv*){pending=false;}
jboolean Instance(JNIEnv*,jobject o,jclass){return o==type&&type_valid;}
jint IntField(JNIEnv*,jobject o,jfieldID f){Require((o==owner||o==reinterpret_cast<jobject>(41))&&f==flags_field,"wrong Class flag field");return (interface_class?0x200:0)|(public_class?1:0)|(obsolete?0x00200000:0);}
jobject ObjectField(JNIEnv*,jobject o,jfieldID f){Require(o==type&&f==return_field,"wrong MethodType field");return requested;}
jsize Length(JNIEnv*,jarray a){Require(a==arguments||a==actual_arguments,"wrong parameter array");return a==arguments?parameters.size():actual_parameters.size();}
jobject Element(JNIEnv*,jobjectArray a,jsize i){auto& list=a==arguments?parameters:actual_parameters;Require((a==arguments||a==actual_arguments)&&i>=0&&size_t(i)<list.size(),"wrong parameter element");return list[i];}
jboolean Same(JNIEnv*,jobject a,jobject b){return a==b;}
jobject Reflect(JNIEnv*,jclass c,jmethodID m,jboolean st){Require((c==owner||c==reinterpret_cast<jclass>(41))&&(reinterpret_cast<uintptr_t>(m)==100||reinterpret_cast<uintptr_t>(m)==101)&&bool(st)==(reinterpret_cast<uintptr_t>(m)==100&&static_method),"wrong reflected instance target");return actual_method;}
jmethodID MethodID(JNIEnv*,jclass,const char* n,const char*){return !std::strcmp(n,"getReturnType")?reflected_return:reflected_parameters;}
jobject ObjectMethod(JNIEnv*,jobject o,jmethodID m,va_list){Require(o==actual_method,"wrong reflected receiver");return m==reflected_return?static_cast<jobject>(actual_return):static_cast<jobject>(actual_arguments);}
const char* Utf(JNIEnv*,jstring s,jboolean*){Require(s==name,"wrong lookup name");return requested_name;}
void Release(JNIEnv*,jstring,const char*){}
jint Throw(JNIEnv*,jclass c,const char*){if(reinterpret_cast<uintptr_t>(c)==7)internal_throws++;else if(reinterpret_cast<uintptr_t>(c)==8)missing_throws++;else access_throws++;pending=true;return 0;}
jmethodID StaticID(JNIEnv*,jclass,const char* n,const char*) {return reinterpret_cast<jmethodID>(!std::strcmp(n,"runStatic")?19:!std::strcmp(n,"runInheritedStatic")?25:!std::strcmp(n,"runNonpublicStatic")?26:!std::strcmp(n,"runBridge")?27:14);}
void Run(JNIEnv* jni,jclass,jmethodID method,va_list) {
  if(reinterpret_cast<uintptr_t>(method)==27){
    if(mode==20){pending=false;return;}
    bool saved=static_method;static_method=false;bridge_method=true;
    for(int path=0;path<3;path++){
      if((mode==22&&path==0)||(mode==23&&path==1)||(mode==24&&path==2))continue;
      interface_class=path!=0;recursive_levels=path==2?3:0;
      Require(reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments)==actual_method,
              "probe did not select actual reflected bridge");
    }
    pending=mode==21;bridge_method=false;static_method=saved;interface_class=true;recursive_levels=0;return;
  }
  if(reinterpret_cast<uintptr_t>(method)==26){
    if(mode==16){pending=false;return;}
    lookup_static=true;static_method=true;interface_class=false;public_class=false;
    for(int path=0;path<2;path++){
      if((mode==18&&path==1)||(mode==19&&path==0))continue;
      inherited_target=path==1;
      reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
    }
    pending=mode==17;inherited_target=false;public_class=true;interface_class=true;lookup_static=false;return;
  }
  if(reinterpret_cast<uintptr_t>(method)==25){
    if(mode==14){pending=false;return;}
    lookup_static=true;static_method=true;interface_class=false;inherited_target=true;
    reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
    pending=mode==15; inherited_target=false;interface_class=true;lookup_static=false;return;
  }
  if(mode==9&&reinterpret_cast<uintptr_t>(method)==19){pending=false;return;} // Stock IAE caught without live-argument observation.
  bool saved=static_method;
  if(reinterpret_cast<uintptr_t>(method)==19){lookup_static=true;static_method=false;}
  if(mode==6)Require(!method_handle_lookup::Prepare(jni,test_env),"reentrant Native preparation accepted partial state");
  pending=false;
  reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
  pending=false; // The Java helper catches the access exception.
  if (reinterpret_cast<uintptr_t>(method)==19 && mode!=12) {
    interface_class=false; static_method=true;
    reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
    interface_class=true; pending=false;
  }
  if (reinterpret_cast<uintptr_t>(method)!=19 && mode!=10) {
    static_method=false; recursive_levels=3;
    reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
    pending=false; recursive_levels=0;
  }
  static_method=saved;lookup_static=false;
}
jvmtiError Caps(jvmtiEnv*,const jvmtiCapabilities* c){Require(c->can_access_local_variables,"missing local capability");return mode==3?JVMTI_ERROR_NOT_AVAILABLE:JVMTI_ERROR_NONE;}
jvmtiError Methods(jvmtiEnv*,jclass c,jint* count,jmethodID** out){
 *count=(c==owner||c==reinterpret_cast<jclass>(41))?(missing_selected?0:2):reinterpret_cast<uintptr_t>(c)==1?5:2;*out=static_cast<jmethodID*>(malloc(sizeof(jmethodID)*(*count)));allocations++;
 if((c==owner||c==reinterpret_cast<jclass>(41))&&*count){(*out)[0]=reinterpret_cast<jmethodID>(101);(*out)[1]=reinterpret_cast<jmethodID>(100);}
 else if(reinterpret_cast<uintptr_t>(c)==1){(*out)[0]=instance;(*out)[1]=declared_method;(*out)[2]=class_get_method;(*out)[3]=public_recursive;(*out)[4]=interface_recursive;}
 else if(*count) {(*out)[0]=virtual_method;(*out)[1]=static_lookup;}
 return JVMTI_ERROR_NONE;
}
jvmtiError Name(jvmtiEnv*,jmethodID m,char** n,char** s,char**){
 if(name_failure&&reinterpret_cast<uintptr_t>(m)==100){pending=true;return JVMTI_ERROR_INTERNAL;}
 const char* name="value";const char* signature=reinterpret_cast<uintptr_t>(m)==101?"()Ljava/lang/String;":member_signature;
 if(m==instance||m==declared_method){name=m==instance?"getInstanceMethod":"getDeclaredMethod";signature="(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;";}
 if(m==public_recursive||m==interface_recursive){name=m==public_recursive?"getPublicMethodRecursive":mode==11?"unavailable":"findInterfaceMethod";signature="(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;";}
 if(m==class_get_method){name="getMethod";signature="(Ljava/lang/String;[Ljava/lang/Class;Z)Ljava/lang/reflect/Method;";}
 if(m==virtual_method||m==static_lookup){name=m==virtual_method?"findVirtual":"findStatic";signature="(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;";}
 if(n)*n=Copy(name);if(s)*s=Copy(signature);return JVMTI_ERROR_NONE;
}
jvmtiError Fields(jvmtiEnv*,jclass c,jint* count,jfieldID** out){*count=1;*out=static_cast<jfieldID*>(malloc(sizeof(jfieldID)));allocations++;**out=reinterpret_cast<uintptr_t>(c)==1?flags_field:return_field;return JVMTI_ERROR_NONE;}
jvmtiError FieldName(jvmtiEnv*,jclass,jfieldID f,char** n,char** s,char**){*n=Copy(f==return_field?"rtype":"accessFlags");*s=Copy(f==return_field?"Ljava/lang/Class;":"I");return JVMTI_ERROR_NONE;}
jvmtiError Locals(jvmtiEnv*,jmethodID,jint* n){*n=9;return JVMTI_ERROR_NONE;}
jvmtiError Modifiers(jvmtiEnv*,jmethodID m,jint* f){*f=reinterpret_cast<uintptr_t>(m)==100?((nonpublic?2:static_method?9:1)|(bridge_method?0x40:0)):1;return JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass c,jobject* out){Require(c==owner||c==reinterpret_cast<jclass>(41),"wrong declaring interface");*out=(boot||(declaring_boot&&c==reinterpret_cast<jclass>(41)))?nullptr:reinterpret_cast<jobject>(90);return JVMTI_ERROR_NONE;}
jvmtiError Stack(jvmtiEnv*,jthread t,jint start,jint max,jvmtiFrameInfo* f,jint* count){
 Require(!t&&start>=0&&max==4,"wrong public stack request");
 if(stack_failure&&start>0)return JVMTI_ERROR_OPAQUE_FRAME;
 std::vector<jmethodID> trace{nullptr};
 if(lookup_static)trace.insert(trace.end(),{class_get_method,declared_method,scope?static_lookup:instance});
 else {
  for(int i=0;i<recursive_levels;i++)trace.insert(trace.end(),{public_recursive,malformed_chain?instance:interface_recursive});
  trace.insert(trace.end(),{instance,scope?virtual_method:instance});
 }
 *count=0;
 for(int i=start;i<int(trace.size())&&*count<max;i++)f[(*count)++].method=trace[i];
 return JVMTI_ERROR_NONE;
}
jvmtiError Local(jvmtiEnv*,jthread t,jint depth,jint slot,jobject* out){Require(!t&&depth==(lookup_static?3:2+2*recursive_levels)&&slot==8,"wrong public local slot");local_reads++;if(missing_local)return JVMTI_ERROR_OPAQUE_FRAME;*out=type;return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass c,char** out,char**){*out=Copy(reinterpret_cast<uintptr_t>(c)>=80?"LParam;":requested_kind?"Ljava/lang/String;":"LResult;");return JVMTI_ERROR_NONE;}
int main(int argc,char** argv){try{
 Require(argc==2,"mode required");mode=std::stoi(argv[1]);if(mode==7)test_api=33;
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.DeleteLocalRef=Delete;jt.DeleteGlobalRef=Delete;jt.GetJavaVM=VM;jt.ExceptionCheck=Exception;jt.ExceptionClear=Clear;jt.IsInstanceOf=Instance;jt.GetIntField=IntField;jt.GetObjectField=ObjectField;jt.ThrowNew=Throw;jt.GetStaticMethodID=StaticID;jt.GetMethodID=MethodID;jt.CallObjectMethodV=ObjectMethod;jt.ToReflectedMethod=Reflect;jt.IsSameObject=Same;jt.CallStaticVoidMethodV=Run;jt.GetArrayLength=Length;jt.GetObjectArrayElement=Element;jt.GetStringUTFChars=Utf;jt.ReleaseStringUTFChars=Release;JNIEnv jni{&jt};
 jvmtiInterface_1_ vt{};vt.AddCapabilities=Caps;vt.GetClassMethods=Methods;vt.GetMethodName=Name;vt.GetClassFields=Fields;vt.GetFieldName=FieldName;vt.Deallocate=Deallocate;vt.GetMaxLocals=Locals;vt.GetMethodModifiers=Modifiers;vt.GetClassLoader=Loader;vt.GetStackTrace=Stack;vt.GetLocalObject=Local;vt.GetClassSignature=Signature;jvmtiEnv env{&vt};test_env=&env;
 Require(!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&!installs,"lookup activated before preparation");
 if(mode==4)missing_local=true;
 bool ok=method_handle_lookup::Prepare(&jni,&env);
 if(mode==1||mode==3||mode==11)Require(!ok&&!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&!installs,"missing named/public capability accepted");
 else if(mode==4||mode==5||mode==10){Require(!ok&&!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&installs==1&&unhooks==1,"failed runtime proof accepted");if(mode==5)Require(!method_handle_lookup::Prepare(&jni,&env)&&installs==1,"failed unhook installed another copy");}
 else{
 Require(ok&&installs==1&&unhooks==1,"Native proof left its temporary hook installed");replacement=nullptr;calls=access_throws=internal_throws=local_reads=0;
 install_failure=true;Require(!method_handle_lookup::Enable()&&!replacement,"failed lazy install escaped");install_failure=false;Require(method_handle_lookup::Enable()&&replacement,"retry failed");Require(method_handle_lookup::Enable()&&installs==3,"duplicate installation");
 auto invoke=[&](){pending=false;return reinterpret_cast<decltype(&Original)>(replacement)(&jni,owner,name,arguments);};
 Require(!invoke()&&access_throws==1&&pending&&!calls,"preferred virtual result hid exact static declaration");
 requested_kind=1;Require(invoke()==reflected&&!pending&&calls==1,"wrong return no longer delegates to NSME");requested_kind=0;
 requested=reinterpret_cast<jclass>(71);Require(!invoke()&&pending&&access_throws==2,"return-type loader aliasing preceded static kind");requested=reinterpret_cast<jclass>(70);
 reflected=nullptr;member_signature="(LParam;)LResult;";parameters={reinterpret_cast<jclass>(81)};
 Require(!invoke()&&pending&&access_throws==3&&calls==1,"parameter alias fell through the parameter-only getter");
 reflected=reinterpret_cast<jobject>(42);member_signature="()LResult;";parameters.clear();
 scope=false;Require(invoke()==reflected&&!pending,"ordinary reflection/findStatic intercepted");scope=true;
 static_method=false;Require(invoke()==reflected&&!pending,"parent instance lookup intercepted");static_method=true;
 interface_class=false;Require(invoke()==reflected&&!pending,"ordinary class lookup intercepted");interface_class=true;
 boot=true;Require(invoke()==reflected&&!pending,"boot hidden-API checks bypassed");boot=false;
 obsolete=true;Require(invoke()==reflected&&!pending,"obsolete mirror check bypassed");obsolete=false;
 missing_local=true;Require(!invoke()&&pending&&internal_throws==1,"unreadable local silently selected parent");missing_local=false;
 type_valid=false;Require(!invoke()&&pending&&internal_throws==2,"unverified argument register used");type_valid=true;
 name_failure=true;Require(!invoke()&&pending&&internal_throws==2&&access_throws==3,"pending metadata exception overwritten");name_failure=false;
 static_method=false;interface_class=false;
 requested=reinterpret_cast<jclass>(71);Require(!invoke()&&pending&&access_throws==4,"ordinary instance return alias did not throw access exception");requested=reinterpret_cast<jclass>(70);
 member_signature="(LParam;)LResult;";parameters={reinterpret_cast<jclass>(81)};actual_parameters={reinterpret_cast<jclass>(80)};
 Require(!invoke()&&pending&&access_throws==5,"ordinary instance parameter alias did not throw access exception");
 parameters={reinterpret_cast<jclass>(80)};Require(invoke()==reflected&&!pending,"matching instance identities did not delegate");
 parameters.clear();actual_parameters.clear();member_signature="()LResult;";static_method=true;interface_class=true;
 lookup_static=true;static_method=false;
 if(mode==8||mode==9||mode==12){
  const int before=calls;
  Require(invoke()==reflected&&!pending&&calls==before+1&&access_throws==5,
          "missing static service/proof disabled virtual protection or intercepted unproved static frames");
 }else{
 Require(!invoke()&&pending&&access_throws==6,"inherited instance findStatic was not an access failure");
 requested_kind=1;Require(!invoke()&&pending&&missing_throws==1,"wrong static return did not precede kind checks");requested_kind=0;
 static_method=true;Require(invoke()==reflected&&!pending,"valid static lookup was intercepted");
 requested=reinterpret_cast<jclass>(71);Require(!invoke()&&pending&&access_throws==7,"static return loader alias fell through");requested=reinterpret_cast<jclass>(70);
 member_signature="(LParam;)LResult;";parameters={reinterpret_cast<jclass>(81)};actual_parameters={reinterpret_cast<jclass>(80)};
 Require(!invoke()&&pending&&access_throws==8,"static parameter loader alias fell through");
 interface_class=false;
 Require(!invoke()&&pending,"ordinary static parameter alias fell through");
 parameters={reinterpret_cast<jclass>(80)};
 Require(invoke()==reflected&&!pending,"matching ordinary static identities did not delegate");
 requested=reinterpret_cast<jclass>(71);
 Require(!invoke()&&pending,"ordinary static return alias fell through");
 parameters={reinterpret_cast<jclass>(81)};
 Require(!invoke()&&pending,"ordinary static double alias fell through");
 scope=false;Require(invoke()==reflected&&!pending,"ordinary static reflection intercepted");scope=true;
 public_class=false;
 if(mode>=13&&mode<=19)Require(invoke()==reflected&&!pending,"unproved nonpublic static owner intercepted");
 else Require(!invoke()&&pending,"proved nonpublic static alias escaped");
 public_class=true;
 boot=true;Require(invoke()==reflected&&!pending,"boot static filtering bypassed");boot=false;
 missing_local=true;Require(!invoke()&&pending,"unreadable ordinary static local accepted");missing_local=false;
 requested=reinterpret_cast<jclass>(70); parameters.clear(); actual_parameters.clear();member_signature="()LResult;";
 requested_kind=1;
 if(mode==13||mode==14||mode==15)Require(invoke()==reflected&&!pending,"unproved class resolver intercepted descriptor");
 else Require(!invoke()&&pending,"missing full class descriptor not rejected");
 requested_kind=0;
 inherited_target=true;
 if(mode==13||mode==14||mode==15)Require(invoke()==reflected&&!pending,"unproved inherited resolver activated");
 else {
  Require(invoke()==actual_method&&!pending,"inherited static did not return actual reflected declaration");
  public_class=false;
  if(mode>=16&&mode<=19)Require(invoke()==reflected&&!pending,"failed nonpublic proof enabled inherited resolution");
  else Require(invoke()==actual_method&&!pending,"proved nonpublic inherited target not returned");
  interface_class=true;Require(invoke()==reflected&&!pending,"nonpublic interface scope widened");interface_class=false;
  public_class=true;
  requested=reinterpret_cast<jclass>(71);Require(!invoke()&&pending,"inherited return loader alias escaped");requested=reinterpret_cast<jclass>(70);
  member_signature="(LParam;)LResult;";parameters={reinterpret_cast<jclass>(81)};actual_parameters={reinterpret_cast<jclass>(80)};
  Require(!invoke()&&pending,"inherited parameter loader alias escaped");
  parameters.clear();actual_parameters.clear();member_signature="()LResult;";
  static_method=false;Require(!invoke()&&pending,"inherited instance accepted as static");static_method=true;
  declaring_boot=true;Require(invoke()==reflected&&!pending,"boot declaring member bypassed filtering");declaring_boot=false;
  missing_selected=true;Require(!invoke()&&pending,"selected method missing from public IDs ignored");missing_selected=false;
  scope=false;Require(invoke()==reflected&&!pending,"inherited ordinary reflection intercepted");scope=true;
 }
 inherited_target=false;
 interface_class=true;
 }
 lookup_static=false;static_method=false;bridge_method=true;requested=reinterpret_cast<jclass>(70);
 for(int path=0;path<3;path++){
  interface_class=path!=0;recursive_levels=path==2?9:0;
  Require(invoke()==(mode>=20?reflected:actual_method)&&!pending,"bridge proof did not control exact selection");
  scope=false;Require(invoke()==reflected&&!pending,"ordinary bridge reflection intercepted");scope=true;
  boot=true;Require(invoke()==reflected&&!pending,"bootstrap bridge filtering bypassed");boot=false;
  requested=reinterpret_cast<jclass>(71);Require(!invoke()&&pending,"bridge loader identity escaped");requested=reinterpret_cast<jclass>(70);
 }
 bridge_method=false;recursive_levels=0;interface_class=true;
 lookup_static=false;parameters.clear();actual_parameters.clear();member_signature="()LResult;";
 static_method=false; requested=reinterpret_cast<jclass>(71);
 for(int levels:{1,3,9}){
  recursive_levels=levels;
  Require(!invoke()&&pending,"recursive inherited return identity escaped");
  scope=false;Require(invoke()==reflected&&!pending,"ordinary recursive reflection intercepted");scope=true;
  nonpublic=true;Require(invoke()==reflected&&!pending,"nonpublic recursive declaration intercepted");nonpublic=false;
  malformed_chain=true;Require(invoke()==reflected&&!pending,"unrelated intervening frame intercepted");malformed_chain=false;
  stack_failure=true;Require(!invoke()&&pending,"failed recursive stack service ignored");stack_failure=false;
  requested=reinterpret_cast<jclass>(70);Require(invoke()==reflected&&!pending,"matching inherited type rejected");requested=reinterpret_cast<jclass>(71);
 }
 recursive_levels=0;requested=reinterpret_cast<jclass>(70);
 const int previous_access=access_throws;
 reflected=nullptr;requested_name="missing";Require(!invoke()&&!pending&&access_throws==previous_access,"original missing member failure overwritten");
 }
 Require(allocations==frees,"JVMTI buffers leaked");std::cout<<"Lookup guard case "<<mode<<" PASS\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

'''
def main():
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/method-handle-lookup')
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(dir=evidence) as temp:
        work = Path(temp)
        (work / 'shadowhook.h').write_text('#pragma once\nextern "C" { void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*); void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*); void* shadowhook_hook_sym_addr(void*,void*,void**); int shadowhook_unhook(void*); }\n')
        (work/'android').mkdir();(work/'android/api-level.h').write_text('#pragma once\nextern "C" int android_get_device_api_level();\n')
        source = work/'test.cc'; source.write_text(SOURCE); native=ROOT/'artdcevm/src/main/cpp';binary=work/'test'
        command=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(work),'-I'+str(native),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),str(native/'platform/common/method_handle_lookup.cc'),'-o',str(binary)]
        result=subprocess.run(command,capture_output=True,text=True);(evidence/'compile.log').write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Lookup compilation failed; inspect ignored evidence')
        for mode in range(25):
            result=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,timeout=30);(evidence/f'case-{mode}.log').write_text(result.stdout+result.stderr)
            if result.returncode:raise RuntimeError('Lookup guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()

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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace artdcevm;
int mode, installs, calls, access_throws, internal_throws, allocations, frees, local_reads, unhooks;
bool install_failure, pending, static_method=true, interface_class=true, scope=true, missing_local=false;
bool type_valid=true, name_failure=false, boot=false, obsolete=false;
int requested_kind=0;
const char* requested_name="value";
const char* member_signature="()LResult;";
jobject reflected=reinterpret_cast<jobject>(42), type=reinterpret_cast<jobject>(55);
jclass owner=reinterpret_cast<jclass>(40), requested=reinterpret_cast<jclass>(70);
jstring name=reinterpret_cast<jstring>(99);
jobjectArray arguments=reinterpret_cast<jobjectArray>(56);
std::vector<jclass> parameters;
jmethodID instance=reinterpret_cast<jmethodID>(11), virtual_method=reinterpret_cast<jmethodID>(12);
jfieldID return_field=reinterpret_cast<jfieldID>(13), flags_field=reinterpret_cast<jfieldID>(15);
void* replacement;
jvmtiEnv* test_env;
void Require(bool v,const char* why) { if(!v) throw std::runtime_error(why); }
char* Copy(const char* s) { allocations++; return strdup(s); }
jvmtiError Deallocate(jvmtiEnv*, unsigned char* p) { if(p) {free(p);frees++;}return JVMTI_ERROR_NONE; }
jobject Original(JNIEnv*,jobject,jstring,jobjectArray) {calls++;return reflected;}
extern "C" void* shadowhook_dlopen(const char*) {return reinterpret_cast<void*>(1);}
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*,const char* n) {
  std::string symbol=n;
  if(mode==1 || (mode==2)!=(symbol.find(".__uniq.")!=std::string::npos)) return nullptr;
  return symbol.find("Class_getDeclaredMethodInternal")!=std::string::npos?HookAddress(Original):nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
extern "C" void* shadowhook_hook_sym_addr(void* t,void* r,void** o){installs++;if(install_failure)return nullptr;replacement=r;*o=t;return t;}
extern "C" int shadowhook_unhook(void*) {unhooks++;return mode==5?1:0;}
jclass Find(JNIEnv*,const char* n){return reinterpret_cast<jclass>(std::strstr(n,"InternalError")?7:std::strstr(n,"MethodType")?3:std::strstr(n,"Lookup")?2:std::strstr(n,"Exception")?4:1);}
jobject Global(JNIEnv*,jobject o){return o;}
void Delete(JNIEnv*,jobject){}
jboolean Exception(JNIEnv*){return pending;}
void Clear(JNIEnv*){pending=false;}
jboolean Instance(JNIEnv*,jobject o,jclass){return o==type&&type_valid;}
jint IntField(JNIEnv*,jobject o,jfieldID f){Require(o==owner&&f==flags_field,"wrong Class flag field");return (interface_class?0x201:1)|(obsolete?0x00200000:0);}
jobject ObjectField(JNIEnv*,jobject o,jfieldID f){Require(o==type&&f==return_field,"wrong MethodType field");return requested;}
jsize Length(JNIEnv*,jarray a){Require(a==arguments,"wrong parameter array");return parameters.size();}
jobject Element(JNIEnv*,jobjectArray a,jsize i){Require(a==arguments&&i>=0&&size_t(i)<parameters.size(),"wrong parameter element");return parameters[i];}
const char* Utf(JNIEnv*,jstring s,jboolean*){Require(s==name,"wrong lookup name");return requested_name;}
void Release(JNIEnv*,jstring,const char*){}
jint Throw(JNIEnv*,jclass c,const char*){if(reinterpret_cast<uintptr_t>(c)==7)internal_throws++;else access_throws++;pending=true;return 0;}
jmethodID StaticID(JNIEnv*,jclass,const char*,const char*) {return reinterpret_cast<jmethodID>(14);}
void Run(JNIEnv* jni,jclass,jmethodID,va_list) {
  if(mode==6)Require(!method_handle_lookup::Prepare(jni,test_env),"reentrant Native preparation accepted partial state");
  pending=false;
  reinterpret_cast<decltype(&Original)>(replacement)(jni,owner,name,arguments);
  pending=false; // The Java helper catches the access exception.
}
jvmtiError Caps(jvmtiEnv*,const jvmtiCapabilities* c){Require(c->can_access_local_variables,"missing local capability");return mode==3?JVMTI_ERROR_NOT_AVAILABLE:JVMTI_ERROR_NONE;}
jvmtiError Methods(jvmtiEnv*,jclass c,jint* count,jmethodID** out){
 *count=c==owner?2:1;*out=static_cast<jmethodID*>(malloc(sizeof(jmethodID)*(*count)));allocations++;
 if(c==owner){(*out)[0]=reinterpret_cast<jmethodID>(101);(*out)[1]=reinterpret_cast<jmethodID>(100);} // Virtual is first, as in libcore's preferred result.
 else **out=reinterpret_cast<uintptr_t>(c)==1?instance:virtual_method;
 return JVMTI_ERROR_NONE;
}
jvmtiError Name(jvmtiEnv*,jmethodID m,char** n,char** s,char**){
 if(name_failure&&reinterpret_cast<uintptr_t>(m)==100){pending=true;return JVMTI_ERROR_INTERNAL;}
 if(n)*n=Copy(m==instance?"getInstanceMethod":m==virtual_method?"findVirtual":"value");
 if(s)*s=Copy(m==instance?"(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;":m==virtual_method?"(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;":reinterpret_cast<uintptr_t>(m)==101?"()Ljava/lang/String;":member_signature);
 return JVMTI_ERROR_NONE;
}
jvmtiError Fields(jvmtiEnv*,jclass c,jint* count,jfieldID** out){*count=1;*out=static_cast<jfieldID*>(malloc(sizeof(jfieldID)));allocations++;**out=reinterpret_cast<uintptr_t>(c)==1?flags_field:return_field;return JVMTI_ERROR_NONE;}
jvmtiError FieldName(jvmtiEnv*,jclass,jfieldID f,char** n,char** s,char**){*n=Copy(f==return_field?"rtype":"accessFlags");*s=Copy(f==return_field?"Ljava/lang/Class;":"I");return JVMTI_ERROR_NONE;}
jvmtiError Locals(jvmtiEnv*,jmethodID,jint* n){*n=9;return JVMTI_ERROR_NONE;}
jvmtiError Modifiers(jvmtiEnv*,jmethodID m,jint* f){*f=reinterpret_cast<uintptr_t>(m)==100&&static_method?9:1;return JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass c,jobject* out){Require(c==owner,"wrong declaring interface");*out=boot?nullptr:reinterpret_cast<jobject>(90);return JVMTI_ERROR_NONE;}
jvmtiError Stack(jvmtiEnv*,jthread t,jint start,jint max,jvmtiFrameInfo* f,jint* count){Require(!t&&!start&&max==3,"wrong public stack request");*count=3;f[1].method=instance;f[2].method=scope?virtual_method:instance;return JVMTI_ERROR_NONE;}
jvmtiError Local(jvmtiEnv*,jthread t,jint depth,jint slot,jobject* out){Require(!t&&depth==2&&slot==8,"wrong public local slot");local_reads++;if(missing_local)return JVMTI_ERROR_OPAQUE_FRAME;*out=type;return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass c,char** out,char**){*out=Copy(reinterpret_cast<uintptr_t>(c)>=80?"LParam;":requested_kind?"Ljava/lang/String;":"LResult;");return JVMTI_ERROR_NONE;}
int main(int argc,char** argv){try{
 Require(argc==2,"mode required");mode=std::stoi(argv[1]);
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.DeleteLocalRef=Delete;jt.ExceptionCheck=Exception;jt.ExceptionClear=Clear;jt.IsInstanceOf=Instance;jt.GetIntField=IntField;jt.GetObjectField=ObjectField;jt.ThrowNew=Throw;jt.GetStaticMethodID=StaticID;jt.CallStaticVoidMethodV=Run;jt.GetArrayLength=Length;jt.GetObjectArrayElement=Element;jt.GetStringUTFChars=Utf;jt.ReleaseStringUTFChars=Release;JNIEnv jni{&jt};
 jvmtiInterface_1_ vt{};vt.AddCapabilities=Caps;vt.GetClassMethods=Methods;vt.GetMethodName=Name;vt.GetClassFields=Fields;vt.GetFieldName=FieldName;vt.Deallocate=Deallocate;vt.GetMaxLocals=Locals;vt.GetMethodModifiers=Modifiers;vt.GetClassLoader=Loader;vt.GetStackTrace=Stack;vt.GetLocalObject=Local;vt.GetClassSignature=Signature;jvmtiEnv env{&vt};test_env=&env;
 Require(!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&!installs,"lookup activated before preparation");
 if(mode==4)missing_local=true;
 bool ok=method_handle_lookup::Prepare(&jni,&env);
 if(mode==1||mode==3)Require(!ok&&!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&!installs,"missing named/public capability accepted");
 else if(mode==4||mode==5){Require(!ok&&!method_handle_lookup::Available()&&!method_handle_lookup::Enable()&&installs==1&&unhooks==1,"failed runtime proof accepted");if(mode==5)Require(!method_handle_lookup::Prepare(&jni,&env)&&installs==1,"failed unhook installed another copy");}
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
 reflected=nullptr;requested_name="missing";Require(!invoke()&&!pending&&access_throws==3,"original missing member failure overwritten");
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
        source = work/'test.cc'; source.write_text(SOURCE); native=ROOT/'artdcevm/src/main/cpp';binary=work/'test'
        command=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(work),'-I'+str(native),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),str(native/'platform/common/method_handle_lookup.cc'),'-o',str(binary)]
        result=subprocess.run(command,capture_output=True,text=True);(evidence/'compile.log').write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Lookup compilation failed; inspect ignored evidence')
        for mode in range(7):
            result=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,timeout=30);(evidence/f'case-{mode}.log').write_text(result.stdout+result.stderr)
            if result.returncode:raise RuntimeError('Lookup guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()

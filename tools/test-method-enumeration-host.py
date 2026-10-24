#!/usr/bin/env python3
"""Exercise production Class.getMethods refinement with JNI/JVMTI service doubles."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/method_enumeration.h"
#include "platform/common/art_hook.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace artdcevm;
template<class T> T id(int n){return reinterpret_cast<T>(static_cast<uintptr_t>(n));}
int num(const void* p){return static_cast<int>(reinterpret_cast<uintptr_t>(p));}
void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int mode,installs,unhooks,calls,allocations,frees,nested_calls;
bool pending,root_interface,bridge=true,wrong_stack,opaque_instance,opaque_local,wrong_list;
bool boot_candidate,boot_root,boot_ancestor,unrelated,class_priority,static_candidate,static_old;
bool alias_return,alias_parameter,different_name,pending_flags,pending_set,nest,install_failure;
bool original_failure;
bool mixed_array,own_root,array_failure,set_failure;
int filtered_size=-1,array_sets=0;
jobject previous=id<jobject>(101), selected=previous;
JNIEnv* active_jni;jvmtiEnv* active_env;
void* replacement;
using Entry=jobjectArray (*)(JNIEnv*,jobject,jboolean);
jobjectArray Original(JNIEnv*,jobject,jboolean){++calls;if(original_failure){pending=true;return nullptr;}return id<jobjectArray>(500);}
extern "C" void* shadowhook_dlopen(const char*){return id<void*>(1);}
extern "C" void shadowhook_dlclose(void*){}
extern "C" void* shadowhook_dlsym(void*,const char* n){
 if(mode==1)return nullptr;
 if((mode==2)!=(std::strstr(n,".__uniq.")!=nullptr))return nullptr;
 return artdcevm::HookAddress(Original);
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
extern "C" void* shadowhook_hook_sym_addr(void* target,void* next,void** original){++installs;if(install_failure)return nullptr;replacement=next;*original=target;return target;}
extern "C" int shadowhook_unhook(void*){++unhooks;return mode==10?1:0;}
jclass Find(JNIEnv*,const char* n){
 if(std::strstr(n,"MethodEnumerationProbe")){if(mode==17){pending=true;return nullptr;}return id<jclass>(5);}
 return id<jclass>(!std::strcmp(n,"java/lang/Class")?1:!std::strcmp(n,"java/lang/reflect/Method")?2:!std::strcmp(n,"java/util/List")?3:4);
}
jobject Global(JNIEnv*,jobject o){if((mode==20&&num(o)==3)||(mode==21&&num(o)==4)||(mode==24&&num(o)==2)){pending=true;return nullptr;}return o;}void Delete(JNIEnv*,jobject){}
jboolean Exception(JNIEnv*){return pending;}void Clear(JNIEnv*){pending=false;}
jint Throw(JNIEnv*,jclass,const char*){Require(!pending,"overwrote pending exception");pending=true;return 0;}
jboolean Same(JNIEnv*,jobject a,jobject b){return a==b;}
jboolean Instance(JNIEnv*,jobject o,jclass c){return !wrong_list&&num(o)==600&&num(c)==3;}
jboolean Assignable(JNIEnv*,jclass sub,jclass sup){Require(num(sub)==40,"reversed assignability query");return num(sup)==41;}
jmethodID MethodId(JNIEnv*,jclass,const char* n,const char*){
 if(!std::strcmp(n,"runStatic"))return mode>=22&&mode!=23&&mode!=58?id<jmethodID>(39):nullptr;
 if(!std::strcmp(n,"runBootstrapOverride")){if(mode==48){pending=true;return nullptr;}return mode>=47?id<jmethodID>(41):nullptr;}
 if(!std::strcmp(n,"runBootstrapStatic")){if(mode==36){pending=true;return nullptr;}return mode>=35?id<jmethodID>(40):nullptr;}
 const char* names[]={"size","get","set","getModifiers","getDeclaringClass","getName","getReturnType","getParameterTypes","run"};
 for(int i=0;i<9;++i)if(!std::strcmp(n,names[i]))return id<jmethodID>(30+i);
 return nullptr;
}
jint IntCall(JNIEnv*,jobject o,jmethodID m,va_list){
 Require(!pending,"JNI int call with pending exception");
 if(num(m)==30){Require(num(o)==600,"wrong list");return 1;}
 Require(num(m)==33&&(num(o)==100||num(o)==101||num(o)==102),"wrong modifier request");
 if(pending_flags){pending=true;return 0;}
 return 1|((num(o)==100?static_candidate:num(o)==101&&static_old)?8:0)|((num(o)==100&&bridge)?0x40:0);
}
jobject ObjectCall(JNIEnv* jni,jobject o,jmethodID m,va_list args){
 Require(!pending,"JNI object call with pending exception");
 int method=num(m),obj=num(o);
 if(method==31){Require(obj==600&&va_arg(args,int)==0,"wrong list get");return selected;}
 if(method==32){
  Require(obj==600&&va_arg(args,int)==0,"wrong list set");jobject value=va_arg(args,jobject);
  Require(num(value)==100,"non-ART method replacement");
  if(pending_set){pending=true;return nullptr;}
  if(nest){nest=false;++nested_calls;Require(reinterpret_cast<Entry>(replacement)(jni,id<jobject>(40),JNI_TRUE)==id<jobjectArray>(500),"nested enumeration failed");}
  jobject old=selected;selected=value;return old;
 }
 Require(obj==100||obj==101||obj==102,"wrong reflected method");
 if(method==34)return id<jobject>(obj!=101?40:class_priority?44:unrelated?43:41);
 if(method==35)return id<jobject>(obj==102?202:obj==100?200:201);
 if(method==36)return id<jobject>(obj==101&&alias_return?71:70);
 if(method==37)return id<jobject>(obj==100?400:401);
 throw std::runtime_error("unknown reflection service");
}
const char* Utf(JNIEnv*,jstring s,jboolean*){return num(s)==202?"instance":num(s)==201&&different_name?"other":"value";}
void Release(JNIEnv*,jstring,const char*){}
jsize Length(JNIEnv*,jarray a){Require(num(a)==400||num(a)==401||num(a)==500||num(a)==501,"wrong array");return num(a)==501?filtered_size:num(a)==500&&mixed_array?2:1;}
jobject Element(JNIEnv*,jobjectArray a,jsize i){Require(i==0||(num(a)==500&&mixed_array&&i==1),"wrong array index");return id<jobject>(num(a)==500?(i==0?100:102):num(a)==401&&alias_parameter?81:80);}
jobjectArray NewArray(JNIEnv*,jsize size,jclass type,jobject value){Require(num(type)==2&&!value,"wrong Method array type");if(array_failure){pending=true;return nullptr;}filtered_size=size;return id<jobjectArray>(501);}
void SetArray(JNIEnv*,jobjectArray a,jsize i,jobject v){Require(num(a)==501&&i==0&&num(v)==102,"wrong retained Method or order");++array_sets;if(set_failure)pending=true;}
jvmtiError Caps(jvmtiEnv*,const jvmtiCapabilities* c){Require(c->can_access_local_variables,"missing local capability");return mode==3?JVMTI_ERROR_NOT_AVAILABLE:JVMTI_ERROR_NONE;}
jvmtiError Deallocate(jvmtiEnv*,unsigned char* p){if(p){++frees;free(p);}return JVMTI_ERROR_NONE;}
char* Copy(const char* p){++allocations;return strdup(p);}
jvmtiError Methods(jvmtiEnv*,jclass c,jint* count,jmethodID** out){Require(num(c)==1,"wrong boot identity owner");*count=2;*out=static_cast<jmethodID*>(malloc(2*sizeof(jmethodID)));++allocations;(*out)[0]=id<jmethodID>(10);(*out)[1]=id<jmethodID>(11);return JVMTI_ERROR_NONE;}
jvmtiError Name(jvmtiEnv*,jmethodID m,char** n,char** s,char**){*n=Copy(num(m)==10?(mode==11?"missing":"getPublicMethodsInternal"):"getMethods");*s=Copy(num(m)==10?"(Ljava/util/List;)V":"()[Ljava/lang/reflect/Method;");return JVMTI_ERROR_NONE;}
jvmtiError Locals(jvmtiEnv*,jmethodID m,jint* n){Require(num(m)==10,"wrong live frame");*n=9;return mode==18?JVMTI_ERROR_ABSENT_INFORMATION:JVMTI_ERROR_NONE;}
jvmtiError Modifiers(jvmtiEnv*,jclass c,jint* f){*f=1|((num(c)==42?!root_interface:num(c)==44)?0:0x200);return JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass c,jobject* out){*out=((num(c)==40&&boot_candidate)||(num(c)==42&&boot_root)||(num(c)==41&&boot_ancestor))?nullptr:id<jobject>(90);return JVMTI_ERROR_NONE;}
jvmtiError Stack(jvmtiEnv*,jthread t,jint start,jint max,jvmtiFrameInfo* f,jint* count){Require(!t&&start==0&&max==3,"wrong stack query");*count=3;f[1].method=id<jmethodID>(wrong_stack?99:10);f[2].method=id<jmethodID>(11);return JVMTI_ERROR_NONE;}
jvmtiError LocalInstance(jvmtiEnv*,jthread t,jint depth,jobject* out){Require(!t&&depth==1,"wrong instance frame");*out=id<jobject>(own_root?40:42);return opaque_instance?JVMTI_ERROR_OPAQUE_FRAME:JVMTI_ERROR_NONE;}
jvmtiError LocalObject(jvmtiEnv*,jthread t,jint depth,jint slot,jobject* out){Require(!t&&depth==1&&slot==8,"wrong list local");*out=id<jobject>(600);return opaque_local?JVMTI_ERROR_OPAQUE_FRAME:JVMTI_ERROR_NONE;}
void Run(JNIEnv* jni,jclass,jmethodID m,va_list){
 if(num(m)==41){
  if(mode==56)Require(!method_enumeration::Prepare(jni,active_env),"nested override proof admitted");
  boot_ancestor=true;pending_set=mode==55;alias_return=mode==57;opaque_instance=mode==59;opaque_local=mode==60;
  for(int path=0;path<4;++path){
   if(mode==49||(mode>=50&&mode<=53&&path==mode-50))continue;
   root_interface=path%2==0;bridge=path<2;selected=previous;
   reinterpret_cast<Entry>(replacement)(jni,id<jobject>(40),JNI_TRUE);
   if(pending)break;
  }
  if(mode==54)pending=true;
  boot_ancestor=root_interface=pending_set=alias_return=opaque_instance=opaque_local=false;bridge=true;
  return;
 }
 if(num(m)==40){
  if(mode==44)Require(!method_enumeration::Prepare(jni,active_env),"nested bootstrap proof admitted");
  array_failure=mode==42;set_failure=mode==43;opaque_instance=mode==45;opaque_local=mode==46;
  static_candidate=boot_candidate=mixed_array=true;
  for(int path=0;path<3;++path){
   if(mode==37||(mode==38&&path==1)||(mode==39&&path==0)||(mode==40&&path==2))continue;
   root_interface=path!=1;own_root=path==2;selected=previous;
   reinterpret_cast<Entry>(replacement)(jni,id<jobject>(40),JNI_TRUE);
   if(pending)break;
  }
  if(mode==41)pending=true;
  static_candidate=boot_candidate=mixed_array=root_interface=own_root=array_failure=set_failure=opaque_instance=opaque_local=false;
  return;
 }
 if(num(m)==39){
  if(mode==34)Require(!method_enumeration::Prepare(jni,active_env),"nested static proof admitted");
  array_failure=mode==32;set_failure=mode==33;static_candidate=true;
  for(int path=0;path<3;++path){
   if(mode==25||(mode==26&&path==1)||(mode==27&&path==0)||(mode==28&&path==2))continue;
   root_interface=path!=1;own_root=path==2;
   for(int mix=0;mix<2;++mix){
    if((mode==29&&mix==0)||(mode==30&&mix==1))continue;
    mixed_array=mix;selected=previous;
    reinterpret_cast<Entry>(replacement)(jni,id<jobject>(40),JNI_TRUE);
    if(pending)break;
   }
   if(pending)break;
  }
  if(mode==31)pending=true;
  static_candidate=root_interface=own_root=mixed_array=array_failure=set_failure=false;
  return;
 }
 Require(num(m)==38,"wrong runtime proof");
 if(mode==19)Require(!method_enumeration::Prepare(jni,active_env),"reentrant proof accepted");
 wrong_stack=mode==4;opaque_instance=mode==12;opaque_local=mode==13;wrong_list=mode==14;pending_set=mode==15;original_failure=mode==16;
 for(int path=0;path<4;++path){
  if((mode==6&&path>=2)||(mode==7&&path<2)||(mode==8&&path%2==1)||(mode==9&&path%2==0))continue;
  root_interface=path<2;bridge=path%2==0;selected=previous;
  reinterpret_cast<Entry>(replacement)(jni,id<jobject>(40),JNI_TRUE);
  if(pending)break;
 }
 if(mode==5)pending=true;
 wrong_stack=opaque_instance=opaque_local=wrong_list=pending_set=original_failure=false;
 root_interface=false;bridge=true;
}
int main(int argc,char** argv){try{
 Require(argc==2,"mode required");mode=std::stoi(argv[1]);
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.DeleteGlobalRef=Delete;jt.DeleteLocalRef=Delete;jt.ExceptionCheck=Exception;jt.ExceptionClear=Clear;jt.ThrowNew=Throw;jt.IsSameObject=Same;jt.IsInstanceOf=Instance;jt.IsAssignableFrom=Assignable;jt.GetMethodID=MethodId;jt.GetStaticMethodID=MethodId;jt.CallIntMethodV=IntCall;jt.CallObjectMethodV=ObjectCall;jt.CallStaticVoidMethodV=Run;jt.GetStringUTFChars=Utf;jt.ReleaseStringUTFChars=Release;jt.GetArrayLength=Length;jt.GetObjectArrayElement=Element;jt.NewObjectArray=NewArray;jt.SetObjectArrayElement=SetArray;JNIEnv jni{&jt};active_jni=&jni;
 jvmtiInterface_1_ vt{};vt.AddCapabilities=Caps;vt.GetClassMethods=Methods;vt.GetMethodName=Name;vt.Deallocate=Deallocate;vt.GetMaxLocals=Locals;vt.GetClassModifiers=Modifiers;vt.GetClassLoader=Loader;vt.GetStackTrace=Stack;vt.GetLocalInstance=LocalInstance;vt.GetLocalObject=LocalObject;jvmtiEnv env{&vt};active_env=&env;
 Require(!method_enumeration::Enable()&&!installs,"enabled before proof");
 bool ok=method_enumeration::Prepare(&jni,&env);
 const bool good=mode==0||mode==2||mode==19||mode>=22;
 Require(ok==good&&!pending,"incorrect proof decision or uncleared proof exception");
 if(!good){
  Require(!method_enumeration::Enable(),"failed proof enabled");
  if(mode==10){int old=installs;Require(!method_enumeration::Prepare(&jni,&env)&&installs==old,"duplicate owner after failed unhook");}
 }else{
  Require(installs==1&&unhooks==1,"temporary proof hook retained");
  install_failure=true;Require(!method_enumeration::Enable(),"failed install accepted");install_failure=false;
  Require(method_enumeration::Enable()&&method_enumeration::Enable()&&installs==3,"install ownership or retry failed");
  auto invoke=[&](bool change,bool error=false,jboolean public_only=JNI_TRUE){
   selected=previous;pending=false;
   jobjectArray result=reinterpret_cast<Entry>(replacement)(&jni,id<jobject>(40),public_only);
   Require((error?!result:result==id<jobjectArray>(500))&&pending==error,"wrong result/exception");
   Require(selected==(change?id<jobject>(100):previous),"wrong declaration replacement");pending=false;
  };
  invoke(true);bridge=false;invoke(true);bridge=true;
  wrong_stack=true;invoke(false);wrong_stack=false;invoke(false,false,JNI_FALSE);
  boot_candidate=true;invoke(false);boot_candidate=false;boot_root=true;invoke(false);boot_root=false;
  boot_ancestor=true;invoke(mode==47||mode==56||mode==58);
  if(mode==47||mode==56||mode==58){
   alias_return=true;invoke(false);alias_return=false;alias_parameter=true;invoke(false);alias_parameter=false;
   unrelated=true;invoke(false);unrelated=false;class_priority=true;invoke(false);class_priority=false;
   opaque_local=true;invoke(false,true);opaque_local=false;pending_set=true;invoke(false,true);pending_set=false;
  }
  boot_ancestor=false;
  unrelated=true;invoke(false);unrelated=false;class_priority=true;invoke(false);class_priority=false;
  if(mode<35&&mode!=22&&mode!=34){static_candidate=true;invoke(false);static_candidate=false;}static_old=true;invoke(false);static_old=false;
  alias_return=true;invoke(false);alias_return=false;alias_parameter=true;invoke(false);alias_parameter=false;
  different_name=true;invoke(false);different_name=false;
  opaque_instance=true;invoke(false,true);opaque_instance=false;opaque_local=true;invoke(false,true);opaque_local=false;
  wrong_list=true;invoke(false,true);wrong_list=false;
  pending_flags=true;invoke(false,true);pending_flags=false;pending_set=true;invoke(false,true);pending_set=false;
  original_failure=true;invoke(false,true);original_failure=false;
  nest=true;invoke(true);Require(nested_calls==1,"nested metadata enumeration not exercised");
  if(mode>=22){
   bool active=mode==22||mode==34||(mode>=35&&mode!=58);
   auto filtered=[&](bool filter,bool error=false,jboolean public_only=JNI_TRUE){
    selected=previous;pending=false;filtered_size=-1;array_sets=0;
    auto result=reinterpret_cast<Entry>(replacement)(&jni,id<jobject>(40),public_only);
    Require((error?!result:result==id<jobjectArray>(filter?501:500))&&pending==error,"static filtering result/exception");
    if(filter&&!error){Require(filtered_size==(mixed_array?1:0),"wrong filtered array length");Require(array_sets==(mixed_array?1:0),"retained Method not copied");}
    pending=false;
   };
   static_candidate=true;filtered(active);mixed_array=true;filtered(active);
   own_root=true;filtered(false);own_root=false;
   wrong_stack=true;filtered(false);wrong_stack=false;
   boot_root=true;filtered(false);boot_root=false;boot_candidate=true;filtered(mode==35||mode==44||(mode>=47&&mode!=58));boot_candidate=false;
   filtered(false,false,JNI_FALSE);
   if(active){array_failure=true;filtered(false,true);array_failure=false;set_failure=true;filtered(false,true);set_failure=false;opaque_local=true;filtered(false,true);opaque_local=false;}
   if(mode>=35){
    const bool boot_active=mode==35||mode==44||(mode>=47&&mode!=58);
    boot_candidate=true;filtered(boot_active);
    root_interface=true;filtered(boot_active);root_interface=false;
    own_root=true;opaque_local=true;filtered(false);opaque_local=false;own_root=false;
    wrong_stack=true;filtered(false);wrong_stack=false;filtered(false,false,JNI_FALSE);
    if(boot_active){array_failure=true;filtered(false,true);array_failure=false;set_failure=true;filtered(false,true);set_failure=false;opaque_instance=true;filtered(false,true);opaque_instance=false;}
    static_candidate=false;filtered(false);Require(selected==previous,"bootstrap instance specificity was changed");
    boot_candidate=false;
   }
   static_candidate=mixed_array=false;
  }
 }
 Require(allocations==frees,"JVMTI identity buffers leaked");
 std::cout<<"Enumeration guard case "<<mode<<" PASS\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
'''
def main():
    evidence=new_evidence_directory(ROOT/'build/evidence/host/method-enumeration')
    helper=runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))
    include=helper['java_home'](None)/'include'
    platform='darwin' if (include/'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(dir=evidence) as temp:
        work=Path(temp); (work/'android').mkdir()
        (work/'shadowhook.h').write_text('#pragma once\nextern "C" { void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*); void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*); void* shadowhook_hook_sym_addr(void*,void*,void**); int shadowhook_unhook(void*); }\n')
        source=work/'test.cc';source.write_text(SOURCE);native=ROOT/'artdcevm/src/main/cpp';binary=work/'test'
        command=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(work),'-I'+str(native),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),str(native/'platform/common/method_enumeration.cc'),'-o',str(binary)]
        result=subprocess.run(command,capture_output=True,text=True);(evidence/'compile.log').write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Enumeration compilation failed; inspect ignored evidence')
        for mode in range(61):
            result=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,timeout=30);(evidence/f'case-{mode}.log').write_text(result.stdout+result.stderr)
            if result.returncode:raise RuntimeError('Enumeration guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()

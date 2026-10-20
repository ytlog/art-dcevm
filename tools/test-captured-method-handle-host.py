#!/usr/bin/env python3
"""Exercise the production rooted capture continuation and every failing upcall."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/captured_method_handle.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
template<class T> T Id(int n){return reinterpret_cast<T>(static_cast<uintptr_t>(n));}
void Need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int mode=0,step=0,frames=0,allocations=0,stores=0,copies=0,invocations=0,returns=0,throws=0;
bool pending=false,active=false;
JNIEnv* jni;JavaVM* vm;
bool Tick(){if(!active)return false;++step;if(mode==step){pending=true;return true;}return false;}
const char* field_names[]={"delegate","receiver","range","type","references"};
const char* field_types[]={"Ljava/lang/invoke/MethodHandle;","Ljava/lang/Object;","Ldalvik/system/EmulatedStackFrame$Range;","Ljava/lang/invoke/MethodType;","[Ljava/lang/Object;"};
const char* method_names[]={"create","copyRangeTo","copyReturnValueTo","invokeFromTransform"};
const char* method_types[]={"(Ljava/lang/invoke/MethodType;)Ldalvik/system/EmulatedStackFrame;","(Ldalvik/system/EmulatedStackFrame;Ldalvik/system/EmulatedStackFrame$Range;II)V","(Ldalvik/system/EmulatedStackFrame;)V","(Ljava/lang/invoke/MethodHandle;Ldalvik/system/EmulatedStackFrame;)V"};
jclass JNICALL Find(JNIEnv*,const char* name){Need(!pending,"class lookup with pending exception");if((mode==100&&strstr(name,"$BindTo"))||(mode==101&&strstr(name,"EmulatedStackFrame"))||(mode==102&&strstr(name,"$Transformer"))||(mode==103&&strstr(name,"InternalError"))){pending=true;return nullptr;}return Id<jclass>(strstr(name,"$BindTo")?1:strstr(name,"$Transformer")?4:strstr(name,"EmulatedStackFrame")?3:strstr(name,"InternalError")?5:2);}
jobject JNICALL Global(JNIEnv*,jobject o){Need(!pending,"global reference with pending exception");if(mode==109){pending=true;return nullptr;}return o;}
jint JNICALL Push(JNIEnv*,jint n){Need(n==16,"unbounded local capacity");if(Tick())return -1;++frames;return 0;}
jobject JNICALL Pop(JNIEnv*,jobject){Need(frames==1,"leaked or nested local frame");--frames;return nullptr;}
jboolean JNICALL Exception(JNIEnv*){return pending;}
jint JNICALL Throw(JNIEnv*,jclass c,const char*){Need(!pending&&c==Id<jclass>(5),"replaced pending exception");pending=true;++throws;return 0;}
jint JNICALL Vm(JNIEnv*,JavaVM** value){*value=vm;return mode==108?JNI_ERR:JNI_OK;}
jint JNICALL Env(JavaVM*,void** value,jint version){Need(version==JNI_VERSION_1_6,"changed JNI version");*value=jni;return mode==90?JNI_ERR:JNI_OK;}
jvmtiError JNICALL Fields(jvmtiEnv*,jclass,jint* n,jfieldID** ids){*n=5;*ids=static_cast<jfieldID*>(malloc(5*sizeof(jfieldID)));++allocations;for(int i=0;i<5;++i)(*ids)[i]=Id<jfieldID>(10+i);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL FieldName(jvmtiEnv*,jclass,jfieldID id,char** n,char** s,char**){int i=reinterpret_cast<uintptr_t>(id)-10;*n=strdup(field_names[i]);*s=strdup(mode==104&&i==2?"wrong":field_types[i]);allocations+=2;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Methods(jvmtiEnv*,jclass,jint* n,jmethodID** ids){*n=4;*ids=static_cast<jmethodID*>(malloc(4*sizeof(jmethodID)));++allocations;for(int i=0;i<4;++i)(*ids)[i]=Id<jmethodID>(20+i);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL MethodName(jvmtiEnv*,jmethodID id,char** n,char** s,char**){int i=reinterpret_cast<uintptr_t>(id)-20;*n=strdup(method_names[i]);*s=strdup(mode==105&&i==3?"wrong":method_types[i]);allocations+=2;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Free(jvmtiEnv*,unsigned char* p){free(p);--allocations;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Max(jvmtiEnv*,jmethodID id,jint* n){Need(id==Id<jmethodID>(101),"decoded opaque method ID");*n=mode==106?1:9;return mode==107?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError JNICALL Location(jvmtiEnv*,jthread,jint depth,jmethodID* id,jlocation* pc){Need(depth==0,"wrong frame depth");if(Tick())return JVMTI_ERROR_OPAQUE_FRAME;*id=Id<jmethodID>(mode==80?102:101);*pc=mode==81?1:0;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Local(jvmtiEnv*,jthread,jint,jobject* value){if(Tick())return JVMTI_ERROR_OPAQUE_FRAME;*value=mode==82?nullptr:Id<jobject>(201);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Argument(jvmtiEnv*,jthread,jint depth,jint slot,jobject* value){Need(depth==0&&slot==8,"wrong DEX input register");if(Tick())return JVMTI_ERROR_OPAQUE_FRAME;*value=mode==83?nullptr:Id<jobject>(202);return JVMTI_ERROR_NONE;}
jobject JNICALL Field(JNIEnv*,jobject object,jfieldID id){Need(!pending,"upcall after exception");if(Tick())return nullptr;int i=reinterpret_cast<uintptr_t>(id);if(object==Id<jobject>(201))return Id<jobject>(i==10?203:i==11?204:205);if(object==Id<jobject>(203)&&i==13)return Id<jobject>(206);if(object==Id<jobject>(207)&&i==14)return Id<jobject>(208);throw std::runtime_error("wrong object field");}
jobject JNICALL Create(JNIEnv*,jclass c,jmethodID m,va_list args){Need(!pending&&c==Id<jclass>(3)&&m==Id<jmethodID>(20)&&va_arg(args,jobject)==Id<jobject>(206),"wrong frame constructor");if(Tick())return nullptr;return mode==84?nullptr:Id<jobject>(207);}
jsize JNICALL Length(JNIEnv*,jarray a){Need(!pending&&a==Id<jarray>(208),"wrong references array");if(Tick())return 0;return mode==85?0:1;}
void JNICALL Store(JNIEnv*,jobjectArray a,jsize i,jobject value){Need(!pending&&a==Id<jobjectArray>(208)&&i==0&&value==Id<jobject>(204),"wrong captured slot");if(!Tick())++stores;}
void JNICALL Call(JNIEnv*,jobject o,jmethodID m,va_list args){Need(!pending,"call after pending exception");if(Tick())return;if(m==Id<jmethodID>(21)){Need(stores==1&&o==Id<jobject>(202)&&va_arg(args,jobject)==Id<jobject>(207)&&va_arg(args,jobject)==Id<jobject>(205)&&va_arg(args,int)==1&&va_arg(args,int)==0,"wrong argument copy range");++copies;}else{Need(m==Id<jmethodID>(22)&&invocations==1&&o==Id<jobject>(207)&&va_arg(args,jobject)==Id<jobject>(202),"lost caller return value");++returns;}}
void JNICALL Invoke(JNIEnv*,jclass c,jmethodID m,va_list args){Need(!pending&&copies==1&&c==Id<jclass>(4)&&m==Id<jmethodID>(23)&&va_arg(args,jobject)==Id<jobject>(203)&&va_arg(args,jobject)==Id<jobject>(207),"bypassed libcore graph");if(!Tick())++invocations;}
int main(int argc,char** argv){try{Need(argc==2,"missing fault mode");mode=atoi(argv[1]);
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.PushLocalFrame=Push;jt.PopLocalFrame=Pop;jt.ExceptionCheck=Exception;jt.ThrowNew=Throw;jt.GetJavaVM=Vm;jt.GetObjectField=Field;jt.CallStaticObjectMethodV=Create;jt.GetArrayLength=Length;jt.SetObjectArrayElement=Store;jt.CallVoidMethodV=Call;jt.CallStaticVoidMethodV=Invoke;JNIEnv j{&jt};jni=&j;
 JNIInvokeInterface_ vt{};vt.GetEnv=Env;JavaVM v{&vt};vm=&v;
 jvmtiInterface_1 et{};et.GetClassFields=Fields;et.GetFieldName=FieldName;et.GetClassMethods=Methods;et.GetMethodName=MethodName;et.Deallocate=Free;et.GetMaxLocals=Max;et.GetFrameLocation=Location;et.GetLocalInstance=Local;et.GetLocalObject=Argument;jvmtiEnv e{&et};
 if(mode>=100){Need(!captured_method_handle::Prepare(jni,&e,Id<jmethodID>(101))&&allocations==0,"bad metadata escaped admission");mode=0;pending=false;}
 Need(captured_method_handle::Prepare(jni,&e,Id<jmethodID>(101))&&allocations==0,"incomplete metadata or leaked JVMTI allocations");
 Need(captured_method_handle::Prepare(jni,&e,Id<jmethodID>(101))&&!captured_method_handle::Prepare(jni,&e,Id<jmethodID>(102)),"entry identity changed after publication");
 active=true;if(mode==91)pending=true;
 bool result=captured_method_handle::Complete(mode==0?jni:nullptr);
 Need(frames==0,"leaked local frame");
 if(mode==0)Need(result&&!pending&&stores==1&&copies==1&&invocations==1&&returns==1,"incomplete continuation");
 else Need(!result&&(pending||mode==90)&&returns==0,"failure continued or lost its exception");
 if(mode==91)Need(step==0&&throws==0,"touched pending exception");
 std::cout<<"Captured continuation case "<<mode<<" PASS; steps="<<step<<"\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
'''

def main():
    helper = runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None)/'include'
    platform = 'darwin' if (include/'darwin').is_dir() else 'linux'
    out = new_evidence_directory(ROOT/'build/evidence/host/captured-method-handle')
    with tempfile.TemporaryDirectory(dir=out) as temp:
        work = Path(temp); source = work/'case.cc'; source.write_text(SOURCE); binary=work/'case'
        native=ROOT/'artdcevm/src/main/cpp'
        result=subprocess.run([shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror',
            '-I'+str(native),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),
            str(source),str(native/'platform/common/captured_method_handle.cc'),'-o',str(binary)],capture_output=True,text=True)
        (out/'compile.log').write_text(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError('Capture host compilation failed; inspect ignored evidence')
        cases=[0,*range(1,16),*range(80,86),90,91,*range(100,110)]
        for case in cases:
            result=subprocess.run([str(binary),str(case)],capture_output=True,text=True)
            (out/f'case-{case}.log').write_text(result.stdout+result.stderr)
            if result.returncode: raise RuntimeError(f'Capture case {case} failed; inspect ignored evidence')
    print(f'Captured continuation: {len(cases)} rooted upcall/failure/identity cases PASS')
    print('Evidence: '+str(out.relative_to(ROOT)))

if __name__=='__main__': main()

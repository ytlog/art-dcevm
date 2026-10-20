#!/usr/bin/env python3
"""Exercise public-JVMTI bound-entry preparation, rollback and callback failures."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''#include "platform/common/bound_method_handle.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
template<class T> T Id(int value) { return reinterpret_cast<T>(static_cast<uintptr_t>(value)); }
void Need(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int mode=0, fail=0, frames=0, events=0, breakpoints=0, clears=0, reflects=0, caps=0;
bool pending=false, compatible=true, receiver_null=false, obsolete=false, owner_interface=true, declaration_interface=true;
int kind=4, thrown=0;
jvmtiEnv* environment; JNIEnv* jni;
jclass bind=Id<jclass>(1), handle=Id<jclass>(2), type=Id<jclass>(3), handles=Id<jclass>(4), method_type=Id<jclass>(5);
jmethodID transform=Id<jmethodID>(40);
const char* names[]={"", "delegate", "receiver", "handleKind", "type", "ptypes"};
const char* signatures[]={"", "Ljava/lang/invoke/MethodHandle;", "Ljava/lang/Object;", "I", "Ljava/lang/invoke/MethodType;", "[Ljava/lang/Class;"};
jclass JNICALL Find(JNIEnv*,const char* name) {
 Need(!pending,"JNI class lookup with a pending exception");
 Need(!bound_method_handle::Prepare(jni,environment),"recursive preparation published partial state");
 if(fail==2){pending=true;return nullptr;}
 if(strstr(name,"$BindTo"))return bind;
 if(strcmp(name,"java/lang/invoke/MethodHandle")==0)return handle;
 if(strcmp(name,"java/lang/invoke/MethodType")==0)return type;
 if(strcmp(name,"java/lang/invoke/MethodHandles")==0)return handles;
 if(strcmp(name,"java/lang/reflect/Method")==0)return method_type;
 if(strstr(name,"IncompatibleClassChangeError"))return Id<jclass>(6);
 if(strstr(name,"InternalError"))return Id<jclass>(7);
 if(strstr(name,"BoundMethodHandleProbe"))return Id<jclass>(8);
 throw std::runtime_error("unexpected bootstrap class");
}
jobject JNICALL Global(JNIEnv*,jobject value){if(fail==10){pending=true;return nullptr;}return value;}
jint JNICALL Push(JNIEnv*,jint){++frames;return 0;}
jobject JNICALL Pop(JNIEnv*,jobject){--frames;return nullptr;}
jboolean JNICALL Exception(JNIEnv*){return pending;}
jint JNICALL Throw(JNIEnv*,jclass klass,const char*){Need(!pending,"replaced a pending exception");thrown=static_cast<int>(reinterpret_cast<uintptr_t>(klass));pending=true;return 0;}
jmethodID JNICALL Method(JNIEnv*,jclass,const char*,const char*){Need(!pending,"method lookup with pending exception");return Id<jmethodID>(41);}
jobject JNICALL ObjectField(JNIEnv*,jobject object,jfieldID field){
 Need(!pending,"field read with pending exception");
 int id=static_cast<int>(reinterpret_cast<uintptr_t>(field));
 if((fail==27&&object==Id<jobject>(10)&&id==1)||(fail==29&&object==Id<jobject>(11))){pending=true;return nullptr;}
 if(object==Id<jobject>(10))return id==1?Id<jobject>(11):receiver_null?nullptr:Id<jobject>(12);
 if(object==Id<jobject>(11)&&id==4)return Id<jobject>(13);
 if(object==Id<jobject>(13)&&id==5)return Id<jobject>(14);
 throw std::runtime_error("unexpected object field");
}
jint JNICALL IntField(JNIEnv*,jobject,jfieldID){if(fail==28)pending=true;return kind;}
jsize JNICALL Length(JNIEnv*,jarray){return 1;}
jobject JNICALL Element(JNIEnv*,jobjectArray,jsize index){Need(index==0,"wrong receiver parameter");return Id<jobject>(15);}
jboolean JNICALL Instance(JNIEnv*,jobject,jclass){return compatible;}
jobject JNICALL Reflect(JNIEnv*,jclass,jmethodID,va_list){++reflects;if(fail==16){pending=true;return nullptr;}return fail==22?nullptr:Id<jobject>(17);}
jmethodID JNICALL FromReflection(JNIEnv*,jobject){return Id<jmethodID>(55);}
void JNICALL Probe(JNIEnv*,jclass,jmethodID,va_list){
 if(fail==5||fail==25||fail==26)return;
 bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),transform,0);
 if(fail==6)pending=true;
}
jvmtiError JNICALL Capabilities(jvmtiEnv*,const jvmtiCapabilities* cap){++caps;Need(cap->can_generate_breakpoint_events&&cap->can_access_local_variables,"missing public capabilities");return fail==1?JVMTI_ERROR_NOT_AVAILABLE:JVMTI_ERROR_NONE;}
jvmtiError JNICALL Fields(jvmtiEnv*,jclass klass,jint* count,jfieldID** result){
 if(fail==3)return JVMTI_ERROR_INTERNAL;
 *count=klass==type?1:2;*result=static_cast<jfieldID*>(malloc(sizeof(jfieldID)**count));
 int first=klass==bind?1:klass==handle?3:5;
 for(int i=0;i<*count;i++)(*result)[i]=Id<jfieldID>(first+i);
 return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL FieldName(jvmtiEnv*,jclass,jfieldID field,char** name,char** signature,char**){
 int id=static_cast<int>(reinterpret_cast<uintptr_t>(field));*name=strdup(names[id]);*signature=strdup(signatures[id]);return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Methods(jvmtiEnv*,jclass,jint* count,jmethodID** result){*count=1;*result=static_cast<jmethodID*>(malloc(sizeof(jmethodID)));**result=transform;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL MethodName(jvmtiEnv*,jmethodID,char** name,char** signature,char**){*name=strdup("transform");*signature=strdup("(Ldalvik/system/EmulatedStackFrame;)V");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Free(jvmtiEnv*,unsigned char* value){free(value);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Location(jvmtiEnv*,jmethodID,jlocation* first,jlocation* last){*first=fail==7?1:0;*last=4;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Notify(jvmtiEnv*,jvmtiEventMode action,jvmtiEvent event,jthread thread,...){Need(event==JVMTI_EVENT_BREAKPOINT&&!thread,"changed another event/thread");if(fail==8&&action==JVMTI_ENABLE)return JVMTI_ERROR_NOT_AVAILABLE;if(fail==26&&action==JVMTI_DISABLE)return JVMTI_ERROR_INTERNAL;events=action==JVMTI_ENABLE;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Set(jvmtiEnv*,jmethodID method,jlocation pc){Need(method==transform&&pc==0,"wrong entry breakpoint");if(fail==4)return JVMTI_ERROR_INTERNAL;if(breakpoints)return JVMTI_ERROR_DUPLICATE;++breakpoints;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Clear(jvmtiEnv*,jmethodID method,jlocation pc){Need(method==transform&&pc==0,"cleared another breakpoint");++clears;if(fail==25)return JVMTI_ERROR_INTERNAL;--breakpoints;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Local(jvmtiEnv*,jthread thread,jint depth,jobject* object){Need(thread==Id<jthread>(18)&&depth==0,"wrong live frame");if(fail==9||fail==19)return JVMTI_ERROR_OPAQUE_FRAME;*object=Id<jobject>(10);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Modifiers(jvmtiEnv*,jclass klass,jint* modifiers){if(fail==24)return JVMTI_ERROR_INTERNAL;*modifiers=(klass==Id<jclass>(15)?owner_interface:declaration_interface)?0x200:0;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Obsolete(jvmtiEnv*,jmethodID member,jboolean* result){Need(member==Id<jmethodID>(55),"treated an ArtMethod as a JNI ID");*result=obsolete;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Declaring(jvmtiEnv*,jmethodID,jclass* result){if(fail==20)return JVMTI_ERROR_INTERNAL;*result=Id<jclass>(16);return JVMTI_ERROR_NONE;}
int main(int argc,char** argv){try{
 Need(argc==2,"missing case");mode=atoi(argv[1]);
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.PushLocalFrame=Push;jt.PopLocalFrame=Pop;jt.ExceptionCheck=Exception;jt.ThrowNew=Throw;
 jt.GetStaticMethodID=Method;jt.GetObjectField=ObjectField;jt.GetIntField=IntField;jt.GetArrayLength=Length;jt.GetObjectArrayElement=Element;jt.IsInstanceOf=Instance;
 jt.CallStaticObjectMethodV=Reflect;jt.FromReflectedMethod=FromReflection;jt.CallStaticVoidMethodV=Probe;JNIEnv j{&jt};jni=&j;
 jvmtiInterface_1 et{};et.AddCapabilities=Capabilities;et.GetClassFields=Fields;et.GetFieldName=FieldName;et.GetClassMethods=Methods;et.GetMethodName=MethodName;et.Deallocate=Free;
 et.GetMethodLocation=Location;et.SetEventNotificationMode=Notify;et.SetBreakpoint=Set;et.ClearBreakpoint=Clear;et.GetLocalInstance=Local;et.GetClassModifiers=Modifiers;et.IsMethodObsolete=Obsolete;et.GetMethodDeclaringClass=Declaring;
 jvmtiEnv e{&et};environment=&e;
 if((mode>=1&&mode<=10)||mode==25||mode==26){fail=mode;Need(!bound_method_handle::Prepare(jni,environment),"failed preparation escaped admission");Need(frames==0&&events==(mode==26)&&breakpoints==(mode==25),"failed preparation leaked an unexpected frame/event/breakpoint");fail=0;pending=false;}
 Need(bound_method_handle::Prepare(jni,environment),"valid preparation failed");int previous_caps=caps;Need(bound_method_handle::Prepare(jni,environment)&&caps==previous_caps,"prepared twice");
 reflects=0;compatible=false;pending=false;thrown=0;
 auto call=[&]{bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),transform,0);Need(frames==0,"callback leaked local references");};
 call();Need(!pending&&!reflects,"guard enabled before an accepted narrowing decision");bound_method_handle::Enable();
 if(mode==12)compatible=true;
 if(mode==13)kind=3;
 if(mode==14)receiver_null=true;
 if(mode==15)owner_interface=false;
 if(mode==16||mode==19||mode==20||mode==22||mode==24||mode>=27)fail=mode;
 if(mode==17)obsolete=true;
 if(mode==18)declaration_interface=false;
 if(mode==21)pending=true;
 if(mode==23){bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),Id<jmethodID>(99),0);bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),transform,1);Need(!pending&&!reflects,"another method/location was intercepted");}
 else {
  call();
  if(mode==12||mode==13||mode==14||mode==15||mode==17||mode==18)Need(!pending&&!thrown,"compatible/null/non-instance/obsolete target was rejected");
  else if(mode==16||mode==21||mode>=27)Need(pending&&!thrown,"pending Java exception was replaced");
  else if(mode==19||mode==20||mode==22||mode==24)Need(pending&&thrown==7,"inspection failure did not preserve an explicit error");
  else Need(pending&&thrown==6,"lost-interface receiver did not throw ICCE");
  if(mode==12)Need(!reflects,"compatible fast path performed reflection");
 }
 std::cout<<"Bound entry case "<<mode<<" PASS\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
'''

def main():
    helper = runpy.run_path(str(ROOT / "tools/test-structural-policy-host.py"))
    include = helper["java_home"](None) / "include"
    platform = "darwin" if (include / "darwin").is_dir() else "linux"
    paths = runpy.run_path(str(ROOT / "tools/evidence_paths.py"))
    out = paths["new_evidence_directory"](ROOT / "build/evidence/host/bound-method-handle")
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        work = Path(temporary)
        source = work / "case.cc"
        source.write_text(SOURCE)
        binary = work / "case"
        native = ROOT / "artdcevm/src/main/cpp"
        command = [shutil.which("clang++") or shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I" + str(native), "-I" + str(include), "-I" + str(include / platform), "-I" + str(ROOT / "third_party/jvmti"),
            str(source), str(native / "platform/common/bound_method_handle.cc"), "-o", str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        (out / "compile.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("Bound entry host compilation failed; inspect ignored evidence")
        for case in range(30):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (out / f"case-{case}.log").write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"Bound entry case {case} failed; inspect ignored evidence")
    print("Bound entry: 30 preparation, rollback and callback cases PASS")
    print("Evidence: " + str(out.relative_to(ROOT)))

if __name__ == "__main__":
    main()

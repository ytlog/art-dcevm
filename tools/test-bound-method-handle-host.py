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
#include <thread>
#include "platform/abi/arm64/common/runtime_objects.h"
using namespace artdcevm;
template<class T> T Id(int value) { return reinterpret_cast<T>(static_cast<uintptr_t>(value)); }
void Need(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int mode=0, fail=0, frames=0, events=0, breakpoints=0, clears=0, reflects=0, caps=0;
bool pending=false, compatible=true, receiver_null=false, obsolete=false, owner_interface=true, declaration_interface=true;
int kind=4, thrown=0;
jvmtiEnv* environment; JNIEnv* jni;
jclass bind=Id<jclass>(1), handle=Id<jclass>(2), type=Id<jclass>(3), handles=Id<jclass>(4), method_type=Id<jclass>(5);
jmethodID transform=Id<jmethodID>(40);
struct Frame { void* link=nullptr;void* method=Id<void*>(100);uint32_t pc=0; };
struct Context { void* self;const void* accessor;void* frame; };
using PcFn=bool (*)(void*,const void*,const void*,uint32_t,const void*,void*);
void* add_replacement=nullptr;void (*switch_replacement)(Context*,const void*,const uint16_t*)=nullptr;PcFn pc_replacement=nullptr;
uint8_t instrumentation[64]{};
bool under_suspend=false,external_event=false,external_failure=false;
void* native_listener=nullptr;
bool restore_failure=false;
bool (*inspection_replacement)(void*,void*)=nullptr;
bool Inspected(void*,void* method){return method==Id<void*>(102);}
int native_checks=0,local_reads=0,probe_calls=0,moves=0,bodies=0;
bool RestoreFault(int wanted){if(mode==wanted&&probe_calls==2&&!restore_failure){restore_failure=true;return true;}return false;}
bool Modern(){return mode==31||mode==34||mode==35||mode==36||mode==37||mode==38||mode==41||mode==42||mode==43;}
size_t event_offset(){return Modern()?12:20;}
uint32_t FramePc(void* p,bool finish){auto* frame=static_cast<Frame*>(p);auto pc=frame->pc;if(finish)frame->pc=~uint32_t{0};return pc;}
namespace artdcevm::invoke_kind {
bool CheckBoundReceiver(void* self,void* bound) {
 Need(self==Id<void*>(19)&&bound==Id<void*>(10),"lost the ART frame receiver");
 if(pending)return false;
 ++native_checks;
 if(!compatible){pending=true;thrown=6;return true;}
 return false;
}
}
bool OriginalPc(void*,const void*,const void* frame,uint32_t pc,const void*,void*){
 if(external_failure){pending=true;return false;}
 if(events&&static_cast<const Frame*>(frame)->method==Id<void*>(100))bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),transform,pc);
 return !pending;
}
void OriginalSwitch(Context* context,const void*,const uint16_t*){
 ++bodies;auto* frame=static_cast<Frame*>(context->frame);
 if(instrumentation[event_offset()]){
  auto call=pc_replacement?pc_replacement:OriginalPc;
  call(context->self,context->accessor,frame,frame->pc,instrumentation,nullptr);
 }
}
void Dispatch(void* method=Id<void*>(100),uint32_t pc=0){
 Frame frame;frame.method=method;frame.pc=pc;Context context{Id<void*>(19),nullptr,&frame};
 auto call=switch_replacement?switch_replacement:OriginalSwitch;call(&context,nullptr,nullptr);
}
void* This(const void*,uint16_t inputs){Need(inputs==2,"wrong transform inputs");return mode==39?nullptr:Id<void*>(10);}
bool Move(void*,void* frame,bool skip,bool skip_throw){Need(pending&&skip&&skip_throw,"bypassed ART event exception handling");++moves;if(mode==41){static_cast<Frame*>(frame)->pc=9;pending=false;return true;}return false;}
void Add(void* object,void* value,uint32_t mask){Need(under_suspend&&object==instrumentation&&mask==8,"bad instrumentation observation");if(value!=Id<void*>(200))native_listener=value;instrumentation[event_offset()]=1;if(mode==38)instrumentation[16]=1;}
void AddTrace(void* object,void* value,uint32_t mask,bool trace){Need(!trace,"changed trace policy");Add(object,value,mask);}
extern "C" void* shadowhook_dlopen(const char*) { return mode>=30?Id<void*>(1):nullptr; }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*,const char* name) {
 if(mode==32)return nullptr;
 if(strstr(name,"IsMethodBeingInspected"))return mode>=44&&mode!=45?reinterpret_cast<void*>(Inspected):nullptr;
 if(strstr(name,"AddListener")&&strstr(name,"Ejb"))return Modern()?reinterpret_cast<void*>(AddTrace):nullptr;
 if(strstr(name,"AddListener")&&strstr(name,"Ej"))return Modern()?nullptr:reinterpret_cast<void*>(Add);
 if(strcmp(name,"ExecuteSwitchImplAsm")==0)return reinterpret_cast<void*>(OriginalSwitch);
 if(strstr(name,"GetThisObject"))return reinterpret_cast<void*>(This);
 if(strstr(name,"MoveToExceptionHandler"))return mode==35?nullptr:reinterpret_cast<void*>(Move);
 if(strstr(name,"DoDexPcMoveEvent")){
  if(mode==40)return nullptr;
  const bool modern=strstr(name,"ActiveInstrumentationHandler");
  return modern==Modern()?reinterpret_cast<void*>(OriginalPc):nullptr;
 }
 return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void*,const char*) { return nullptr; }
extern "C" void* shadowhook_hook_sym_addr(void* target,void* replacement,void** original) {
 if((mode==46&&target==reinterpret_cast<void*>(Inspected))||mode==33||(mode==34&&target==reinterpret_cast<void*>(OriginalSwitch)))return nullptr;
 *original=target;
 if(target==reinterpret_cast<void*>(Inspected))inspection_replacement=reinterpret_cast<decltype(inspection_replacement)>(replacement);
 if(target==reinterpret_cast<void*>(Add)||target==reinterpret_cast<void*>(AddTrace))add_replacement=replacement;
 if(target==reinterpret_cast<void*>(OriginalSwitch))switch_replacement=reinterpret_cast<decltype(switch_replacement)>(replacement);
 if(target==reinterpret_cast<void*>(OriginalPc)){Need(!pc_replacement,"hooked an aliased named entry twice");}
 if(target==reinterpret_cast<void*>(OriginalPc))pc_replacement=reinterpret_cast<PcFn>(replacement);
 return target;
}
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
 ++probe_calls;
 if(mode>=30){if(!((mode==50||mode==51||mode==52)&&probe_calls==2))Dispatch();if((mode==37||mode==49)&&probe_calls==2)pending=true;}
 else bound_method_handle::Breakpoint(environment,jni,Id<jthread>(18),transform,0);
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
jvmtiError JNICALL MethodModifiers(jvmtiEnv*,jmethodID,jint* flags){*flags=(mode==42||mode==53)?0x20:0;return (mode==43||mode==54)?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError JNICALL Location(jvmtiEnv*,jmethodID,jlocation* first,jlocation* last){*first=fail==7?1:0;*last=4;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Notify(jvmtiEnv*,jvmtiEventMode action,jvmtiEvent event,jthread thread,...){Need(event==JVMTI_EVENT_BREAKPOINT&&!thread,"changed another event/thread");if((fail==8||RestoreFault(52))&&action==JVMTI_ENABLE)return JVMTI_ERROR_NOT_AVAILABLE;if((fail==26||mode==36||mode==48)&&action==JVMTI_DISABLE)return JVMTI_ERROR_INTERNAL;
 if(action==JVMTI_ENABLE&&add_replacement&&!events){under_suspend=true;
  if(Modern())reinterpret_cast<decltype(&AddTrace)>(add_replacement)(instrumentation,Id<void*>(200),8,false);
  else reinterpret_cast<decltype(&Add)>(add_replacement)(instrumentation,Id<void*>(200),8);
  under_suspend=false;
 }
 if(action==JVMTI_DISABLE&&!external_event)instrumentation[event_offset()]=0;
 events=action==JVMTI_ENABLE;
 instrumentation[event_offset()]=events||external_event||native_listener;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Set(jvmtiEnv*,jmethodID method,jlocation pc){Need(method==transform&&pc==0,"wrong entry breakpoint");if(fail==4||RestoreFault(51))return JVMTI_ERROR_INTERNAL;if(breakpoints)return JVMTI_ERROR_DUPLICATE;++breakpoints;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Clear(jvmtiEnv*,jmethodID method,jlocation pc){Need(method==transform&&pc==0,"cleared another breakpoint");++clears;if(fail==25||mode==47)return JVMTI_ERROR_INTERNAL;if(!breakpoints)return JVMTI_ERROR_NOT_FOUND;--breakpoints;if(inspection_replacement&&mode!=55){if(mode==57)std::thread([]{inspection_replacement(nullptr,Id<void*>(100));}).join();else inspection_replacement(nullptr,Id<void*>(mode==56?102:100));}return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Local(jvmtiEnv*,jthread thread,jint depth,jobject* object){Need(thread==Id<jthread>(18)&&depth==0,"wrong live frame");++local_reads;if(fail==9||fail==19)return JVMTI_ERROR_OPAQUE_FRAME;*object=Id<jobject>(10);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Modifiers(jvmtiEnv*,jclass klass,jint* modifiers){if(fail==24)return JVMTI_ERROR_INTERNAL;*modifiers=(klass==Id<jclass>(15)?owner_interface:declaration_interface)?0x200:0;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Obsolete(jvmtiEnv*,jmethodID member,jboolean* result){Need(member==Id<jmethodID>(55),"treated an ArtMethod as a JNI ID");*result=obsolete;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Declaring(jvmtiEnv*,jmethodID,jclass* result){if(fail==20)return JVMTI_ERROR_INTERNAL;*result=Id<jclass>(16);return JVMTI_ERROR_NONE;}
int main(int argc,char** argv){try{
 Need(argc==2,"missing case");mode=atoi(argv[1]);
 JNINativeInterface_ jt{};jt.FindClass=Find;jt.NewGlobalRef=Global;jt.PushLocalFrame=Push;jt.PopLocalFrame=Pop;jt.ExceptionCheck=Exception;jt.ThrowNew=Throw;
 jt.GetStaticMethodID=Method;jt.GetObjectField=ObjectField;jt.GetIntField=IntField;jt.GetArrayLength=Length;jt.GetObjectArrayElement=Element;jt.IsInstanceOf=Instance;
 jt.CallStaticObjectMethodV=Reflect;jt.FromReflectedMethod=FromReflection;jt.CallStaticVoidMethodV=Probe;JNIEnv j{&jt};jni=&j;
 jvmtiInterface_1 et{};et.AddCapabilities=Capabilities;et.GetClassFields=Fields;et.GetFieldName=FieldName;et.GetClassMethods=Methods;et.GetMethodName=MethodName;et.Deallocate=Free;
 et.GetMethodModifiers=MethodModifiers;et.GetMethodLocation=Location;et.SetEventNotificationMode=Notify;et.SetBreakpoint=Set;et.ClearBreakpoint=Clear;et.GetLocalInstance=Local;et.GetClassModifiers=Modifiers;et.IsMethodObsolete=Obsolete;et.GetMethodDeclaringClass=Declaring;
 jvmtiEnv e{&et};environment=&e;
 if((mode>=1&&mode<=10)||mode==25||mode==26){fail=mode;Need(!bound_method_handle::Prepare(jni,environment),"failed preparation escaped admission");Need(frames==0&&events==(mode==26)&&breakpoints==(mode==25),"failed preparation leaked an unexpected frame/event/breakpoint");fail=0;pending=false;}
 if(mode==37||mode==49||mode==51||mode==52){Need(!bound_method_handle::Prepare(jni,environment)&&!events&&!breakpoints,"failed native probe escaped admission");pending=false;}
 Need(bound_method_handle::Prepare(jni,environment),"valid preparation failed");int previous_caps=caps;Need(bound_method_handle::Prepare(jni,environment)&&caps==previous_caps,"prepared twice");
 if(mode>=30){
  const bool accelerated=mode!=32&&mode!=33&&mode!=39&&mode!=40;
  const bool direct_entry=accelerated&&Modern()&&mode!=34&&mode!=35&&mode!=36&&mode!=38&&mode!=42&&mode!=43;
  const bool policy=mode>=44&&mode!=45&&mode!=46&&mode!=47&&mode!=48&&mode!=50&&mode!=53&&mode!=54&&mode!=55&&mode!=56&&mode!=57;
  Need(events==!(direct_entry||policy),"native probe selected the wrong fallback");
  Need(static_cast<int>(bound_method_handle::Mode())==(policy?3:direct_entry?2:accelerated?1:0),"entry-mode evidence disagrees with the active path");
  if(inspection_replacement){
   Need(inspection_replacement(nullptr,Id<void*>(100))==policy,"inspection policy leaked or lost the bootstrap target");
   Need(!inspection_replacement(nullptr,Id<void*>(101))&&inspection_replacement(nullptr,Id<void*>(102)),"changed another method's inspection policy");
  }
  Need(breakpoints==!policy,"policy transition leaked or lost a breakpoint");
  compatible=false;int before=local_reads;Dispatch();Need(!pending,"guard enabled before narrowing");
  bound_method_handle::Configure(FramePc,invoke_kind::CheckBoundReceiver);bound_method_handle::Enable();compatible=true;Dispatch();Need(!pending,"compatible receiver rejected");
  compatible=false;Dispatch();Need((mode==41?!pending:pending)&&thrown==6,"lost interface not rejected or handler lost");
  if(accelerated)Need(local_reads==before&&native_checks==2,"fast calls used Native stack inspection");
  pending=false;thrown=0;int previous=native_checks;
  Dispatch(Id<void*>(101));Dispatch(Id<void*>(100),1);Need(!pending&&native_checks==previous,"another method/PC was intercepted");
  pending=true;Dispatch();Need(thrown==0,"pending instrumentation exception replaced");
  if(accelerated){
   pending=false;thrown=0;external_event=true;instrumentation[event_offset()]=1;external_failure=true;
   int previous=native_checks;Dispatch();Need(pending&&!thrown&&native_checks==previous,"external event exception precedence changed");
   pending=false;external_failure=false;Dispatch();Need(pending&&thrown==6,"live external events bypassed the receiver guard");
   Need(moves==int(direct_entry),"entry exception did not reuse ART's exception handler");
  }
  Need(frames==0,"JNI frame leaked");
  std::cout<<"Bound native entry case "<<mode<<" PASS\n";return 0;
 }
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
        (work / "shadowhook.h").write_text('#pragma once\nextern "C" {\nvoid* shadowhook_dlopen(const char*);\nvoid shadowhook_dlclose(void*);\nvoid* shadowhook_dlsym(void*,const char*);\nvoid* shadowhook_dlsym_dynsym(void*,const char*);\nvoid* shadowhook_hook_sym_addr(void*,void*,void**);\n}\n')
        source = work / "case.cc"
        source.write_text(SOURCE)
        binary = work / "case"
        native = ROOT / "artdcevm/src/main/cpp"
        command = [shutil.which("clang++") or shutil.which("c++"), "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
            "-I" + str(native), "-I" + str(include), "-I" + str(include / platform), "-I" + str(ROOT / "third_party/jvmti"), "-I" + str(work),
            str(source), str(native / "platform/common/bound_method_handle.cc"), "-o", str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        (out / "compile.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("Bound entry host compilation failed; inspect ignored evidence")
        for case in range(58):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (out / f"case-{case}.log").write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"Bound entry case {case} failed; inspect ignored evidence")
    print("Bound entry: 58 public/native preparation, fallback, rollback and callback cases PASS")
    print("Evidence: " + str(out.relative_to(ROOT)))

if __name__ == "__main__":
    main()

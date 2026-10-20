#!/usr/bin/env python3
"""Exercise production constructor guards, tag lifetime and exception precedence."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <cstdarg>
#include <iostream>
#include <stdexcept>
#include "platform/common/constructor_history.cc"
using namespace artdcevm;
using namespace artdcevm::constructor_history;
template<class T>T Id(int n){return reinterpret_cast<T>(static_cast<uintptr_t>(n));}
void Need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
int mode=0,frames=0,original_calls=0,argument_calls=0,metadata_calls=0,get_tags=0,fallback_calls=0;
int source_kind=4,target_kind=4;
uint32_t owner_flags=1;bool retired=false;int pending=0,cause=0,created=0;
std::shared_ptr<Root> root;Copy* copy;
int checks=0,weak_created=0,weak_deleted=0,public_identity_calls=0;
bool weak_cleared=false,weak_failure=false;
jobject weak_key=nullptr;
void Clean(){Need(!pending,"VM operation with a pending exception");}
jboolean JNICALL Check(JNIEnv*){++checks;return pending!=0;}
jthrowable JNICALL Occurred(JNIEnv*){return Id<jthrowable>(pending);}
void JNICALL Clear(JNIEnv*){pending=0;}
jint JNICALL ThrowNew(JNIEnv*,jclass klass,const char*){Clean();pending=reinterpret_cast<uintptr_t>(klass);return 0;}
jint JNICALL ThrowObject(JNIEnv*,jthrowable error){Clean();pending=reinterpret_cast<uintptr_t>(error)==900?created:reinterpret_cast<uintptr_t>(error);return 0;}
jobject JNICALL NewObject(JNIEnv*,jclass klass,jmethodID,va_list a){Clean();created=reinterpret_cast<uintptr_t>(klass);cause=reinterpret_cast<uintptr_t>(va_arg(a,jobject));return Id<jobject>(900);}
void JNICALL Delete(JNIEnv*,jobject){}
jint JNICALL Push(JNIEnv*,jint n){Clean();Need(n==16,"unbounded roots");++frames;return 0;}
jobject JNICALL Pop(JNIEnv*,jobject result){Need(frames==1,"local frame imbalance");--frames;return result;}
jobject JNICALL ObjectField(JNIEnv*,jobject,jfieldID){Clean();++metadata_calls;if(mode==12){pending=100;return nullptr;}return Id<jobject>(2);}
jint JNICALL IntField(JNIEnv*,jobject object,jfieldID){Clean();return object==Id<jobject>(2)?owner_flags:1;}
jlong JNICALL LongField(JNIEnv*,jobject,jfieldID){Clean();return 0x1000;}
jboolean JNICALL BoolField(JNIEnv*,jobject,jfieldID){Clean();return false;}
jboolean Identity(JNIEnv*,jobject a,jobject b){Clean();return b==Id<jobject>(10000)?!weak_cleared&&a==weak_key:a==b;}
jboolean JNICALL Same(JNIEnv* env,jobject a,jobject b){++public_identity_calls;return Identity(env,a,b);}
jweak JNICALL Weak(JNIEnv*,jobject key){Clean();if(weak_failure){pending=94;return nullptr;}++weak_created;weak_key=key;weak_cleared=false;return Id<jweak>(10000);}
void JNICALL DeleteWeak(JNIEnv*,jweak key){Need(key==Id<jweak>(10000),"deleted a foreign weak reference");++weak_deleted;}

jboolean JNICALL Instance(JNIEnv*,jobject object,jclass klass){Clean();if(klass==Id<jclass>(800))return object==Id<jobject>(701)&&source_kind!=9;if(reinterpret_cast<uintptr_t>(klass)>=600&&reinterpret_cast<uintptr_t>(klass)<608)return object==Id<jobject>(701)&&klass==Id<jclass>(600+source_kind);return object==klass;}
jobject JNICALL CallObject(JNIEnv*,jobject,jmethodID,va_list){Clean();++argument_calls;return Id<jobject>(4);}
jsize JNICALL Length(JNIEnv*,jarray array){Clean();return array==Id<jarray>(4)?1:mode==3?0:1;}
jobject JNICALL Element(JNIEnv*,jobjectArray array,jsize){Clean();if(array==Id<jobjectArray>(4))return Id<jobject>(target_kind==8?800:500+target_kind);return mode==5||source_kind<0?nullptr:Id<jobject>(mode==4||mode==8?702:701);}
jvmtiError JNICALL GetTag(jvmtiEnv*,jobject,jlong* value){Clean();++get_tags;if(mode==11)return JVMTI_ERROR_INTERNAL;*value=mode==13?0:reinterpret_cast<uintptr_t>(copy);return JVMTI_ERROR_NONE;}
jobject Original(JNIEnv*,jobject,jobjectArray){Clean();++original_calls;if(mode==9)pending=90;if(mode==10)pending=82;return Id<jobject>(303);}
jobject Fallback(JNIEnv* env,jobject constructor,jobjectArray args){++fallback_calls;return Original(env,constructor,args);}
jobject JNICALL Global(JNIEnv*,jobject object){Clean();return object;}
jvmtiError JNICALL Current(jvmtiEnv*,jthread* thread){*thread=Id<jthread>(200);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Stack(jvmtiEnv*,jthread,jint depth,jint max,jvmtiFrameInfo* frame,jint* count){
  Need(max==1&&depth<(mode==31?1:mode==32?2:3),"application frame was encoded into a pointer ID");
  ++get_tags;*count=1;frame->method=Id<jmethodID>(mode==32&&depth==1?10:depth==2?9:7);return JVMTI_ERROR_NONE;
}
namespace artdcevm::member_retirement {bool IsRetiredMethod(const void*){return retired;}}
int main(int argc,char**argv){
 try{
  mode=argc>1?std::atoi(argv[1]):0;
  JNINativeInterface_ j{};JNIEnv_ env{&j};
  j.ExceptionCheck=Check;j.ExceptionOccurred=Occurred;j.ExceptionClear=Clear;j.ThrowNew=ThrowNew;j.Throw=ThrowObject;
  j.NewGlobalRef=Global;j.DeleteGlobalRef=Delete;j.NewWeakGlobalRef=Weak;j.DeleteWeakGlobalRef=DeleteWeak;
  j.NewObjectV=NewObject;j.DeleteLocalRef=Delete;j.PushLocalFrame=Push;j.PopLocalFrame=Pop;
  j.GetObjectField=ObjectField;j.GetIntField=IntField;j.GetLongField=LongField;j.GetBooleanField=BoolField;
  j.IsSameObject=Same;j.IsInstanceOf=Instance;j.CallObjectMethodV=CallObject;j.GetArrayLength=Length;j.GetObjectArrayElement=Element;
  jvmtiInterface_1_ table{};jvmtiEnv ti{&table};table.GetTag=GetTag;
  state=new State;state->env=&ti;state->ready=true;
  state->current_thread=[]()->void*{return Id<void*>(1);};
  if(mode==17||mode==26)state->same_object=Identity;
  state->instantiation=Id<jclass>(80);state->missing=Id<jclass>(81);state->invocation=Id<jclass>(82);
  state->illegal_argument=Id<jclass>(83);state->cast_error=Id<jclass>(84);state->null_error=Id<jclass>(85);
  state->initializer_error=Id<jclass>(90);state->linkage_error=Id<jclass>(91);state->access_error=Id<jclass>(92);state->internal_error=Id<jclass>(93);
  for(int i=0;i<8;++i){state->primitive[i]=Id<jclass>(500+i);state->boxed[i]=Id<jclass>(600+i);}
  root=std::make_shared<Root>();copy=new Copy(root);
  if(mode==6)source_kind=1;
  if(mode>=30&&mode<=32){
    state->class_type=Id<jclass>(250);state->decode_global=[](JavaVM*,jobject)->void*{return Id<void*>(1000);};
    state->calling_class=[](void*,size_t depth)->void*{return Id<void*>(depth<static_cast<size_t>(mode==31?1:mode==32?2:3)?1000:999);};
    state->public_lookup=Id<jmethodID>(9);state->declared_lookup=Id<jmethodID>(10);
    table.GetCurrentThread=Current;table.GetStackTrace=Stack;bool public_only=false;
    const bool found=PublicLookup(&env,&public_only);
    Need(found==(mode!=31)&&public_only==(mode==30)&&get_tags==(mode==31?1:mode==32?2:3),"bootstrap lookup boundary changed");
    Freed(&ti,reinterpret_cast<uintptr_t>(copy));std::cout<<"pointer ID discovery boundary PASS\n";return 0;
  }
  if(mode==17||mode==18||mode==19||mode==22||mode==24){
    Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);Need(original_calls==1,"initial constructor failed");
    if(mode==17){
      Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
      Need(get_tags==1&&public_identity_calls==0&&CacheProofs()==3,"proved named weak identity did not reuse history");
    }else if(mode==19){
      weak_failure=true;Invoke(&env,Id<jobject>(5),Id<jobjectArray>(3),Original,Fallback);
      Need(pending==94&&original_calls==1&&weak_deleted==0&&recent.root==root,"weak allocation failure entered body or destroyed prior cache");
    }else if(mode==24){
      std::weak_ptr<Root> weak=root;root.reset();Freed(&ti,reinterpret_cast<uintptr_t>(copy));
      Need(!weak.expired(),"ObjectFree invalidated native accessor still in use");Ended(&env);
      Need(weak.expired()&&weak_deleted==1&&!recent.constructor&&!recent.root&&!recent_address,"thread detach retained native history or weak identity");
      std::cout<<"thread cache cleanup PASS\n";return 0;
    }else{
      if(mode==18)weak_cleared=true;
      Freed(&ti,reinterpret_cast<uintptr_t>(copy));root=std::make_shared<Root>();copy=new Copy(root);owner_flags=0x401;
      Invoke(&env,Id<jobject>(mode==22?5:1),Id<jobjectArray>(3),Original,Fallback);
      Need(pending==80&&original_calls==1&&get_tags==2&&weak_deleted==1,"cleared or different weak key inherited another accessor");
    }
    Ended(&env);Need(weak_created==weak_deleted,"weak cache reference leaked");Freed(nullptr,reinterpret_cast<uintptr_t>(copy));
    std::cout<<"weak cache case "<<mode<<" PASS\n";return 0;
  }
  if(mode==20||mode==21){
    root->accessor=Accessor::kDirect;owner_flags=0x401;
    const char* kinds="ZBCSIJFD";
    const char* widening[]={"Z","BSIJFD","CIJFD","SIJFD","IJFD","JFD","FD","D"};
    for(int target=mode==21?8:0;target<(mode==21?9:8);++target){
      for(int source=-1;source<(mode==21?10:8);++source){
        if(mode==21&&source!=-1&&source!=4&&source!=9)continue;
        target_kind=target;source_kind=source;pending=cause=0;
        Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
        const bool allowed=mode==21?source!=9:source>=0&&std::strchr(widening[source],kinds[target]);
        Need(pending==(allowed?82:83)&&cause==(allowed?80:source<0?85:84),"primitive/reference adaptation matrix differs");
        Need(original_calls==0&&frames==0,"conversion matrix entered a constructor or lost roots");
      }
    }
    Ended(&env);Freed(&ti,reinterpret_cast<uintptr_t>(copy));
    std::cout<<"adaptation matrix PASS\n";return 0;
  }
  if(mode==14){
    std::weak_ptr<Root> weak=root;auto* sibling=new Copy(root);root.reset();
    Freed(&ti,reinterpret_cast<uintptr_t>(copy));Need(!weak.expired(),"freed another copy's accessor");
    Freed(&ti,reinterpret_cast<uintptr_t>(sibling));Need(weak.expired(),"tag cleanup retained an accessor");
    std::cout<<"tag lifecycle PASS\n";return 0;
  }
  if(mode==16){
    state->one_target=Id<void*>(11);state->many_target=Id<void*>(12);state->one_name="one";state->many_name="many";
    std::vector<Hook> hooks;std::string why;Need(Prepare(nullptr,nullptr,&hooks,&why)&&hooks.size()==2,"retry omitted prepared hooks");
    std::cout<<"retry hooks PASS\n";return 0;
  }
  if(mode==15){
    state->ready=false;
    Need(Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback)==Id<jobject>(303),"unavailable history lost legacy construction");
    Need(fallback_calls==1&&get_tags==0&&weak_created==0&&!recent_address,"unavailable history accessed the tag cache");
    Freed(&ti,reinterpret_cast<uintptr_t>(copy));std::cout<<"unavailable history fallback PASS\n";return 0;
  }
  if(mode==25||mode==26){
    Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
    const int before_checks=checks,before_metadata=metadata_calls;
    mode=10; // ART may return a non-null local reference with an exception pending.
    Need(Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback)==Id<jobject>(303),"hot target lost ART's local result");
    Need(pending==82&&original_calls==2&&root->accessor==Accessor::kDirect&&root->public_epoch==1,"hot target failure changed accessor history or exception");
    Need(checks==before_checks&&metadata_calls==before_metadata&&get_tags==1&&fallback_calls==0,"hot target failure repeated slow guards");
    pending=0;
  }else if(mode==0){
    Need(Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback)==Id<jobject>(303),"lost the constructed local root");
    Need(root->accessor==Accessor::kDirect&&root->public_epoch==1,"successful constructor was not warmed");
    int before=metadata_calls,check_before=checks;Need(Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback)==Id<jobject>(303),"fast construction failed");
    Need(get_tags==1&&fallback_calls==0,"proved constructor repeated identity or retirement checks");
    Need(checks==check_before,"hot cache repeated JNI exception checks");
    Need(metadata_calls==before&&original_calls==2,"repeat construction repeated metadata work");
    owner_flags=0x401;Invalidate();Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
    Need(pending==82&&cause==80&&original_calls==2,"commit failed to invalidate a cached allocation verdict");
  }else if(mode==1){
    owner_flags=0x401;Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
    Need(pending==80&&root->accessor==Accessor::kInstantiationError&&argument_calls==0,"cold accessor checked arguments");
    pending=0;owner_flags=1;Invalidate();Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
    Need(pending==80&&original_calls==0&&argument_calls==0,"restoration discarded a cached allocation error");
  }else{
    root->accessor=Accessor::kDirect;
    if(mode>=2&&mode<=6)owner_flags=0x401;
    if(mode==7||mode==8)retired=true;
    Invoke(&env,Id<jobject>(1),Id<jobjectArray>(3),Original,Fallback);
    if(mode==2||mode==6)Need(pending==82&&cause==80&&original_calls==0,"allocation error was not wrapped after adaptation");
    if(mode==3)Need(pending==83&&cause==0&&original_calls==0,"arity did not precede allocation");
    if(mode==4||mode==8)Need(pending==83&&cause==84&&original_calls==0,"conversion did not precede dispatch");
    if(mode==5)Need(pending==83&&cause==85&&original_calls==0,"null unboxing precedence changed");
    if(mode==7)Need(pending==82&&cause==81&&original_calls==0,"retired constructor body ran");
    if(mode==9)Need(pending==90&&root->accessor==Accessor::kEmpty&&root->public_epoch==0,"failed initialization warmed an accessor");
    if(mode==10)Need(pending==82&&root->accessor==Accessor::kDirect,"target failure discarded a valid accessor");
    if(mode==11||mode==12)Need(pending==(mode==11?93:100)&&original_calls==0,"metadata failure entered a constructor");
    if(mode==13)Need(original_calls==1&&fallback_calls==1,"nonstandard lookup lost its existing guard");
  }
  Need(frames==0,"leaked local frame");Ended(&env);Need(!recent.constructor&&!recent.root&&!recent_address,"thread detach retained native cache state");Freed(&ti,reinterpret_cast<uintptr_t>(copy));
  std::cout<<"constructor case "<<mode<<" PASS\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
'''


def main():
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    out = new_evidence_directory(ROOT / 'build/evidence/host/constructor-history')
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        work = Path(temporary)
        (work / 'shadowhook.h').write_text('''#pragma once
inline void* shadowhook_dlopen(const char*) { return nullptr; }
inline void* shadowhook_dlsym(void*, const char*) { return nullptr; }
inline void* shadowhook_dlsym_dynsym(void*, const char*) { return nullptr; }
inline void shadowhook_dlclose(void*) {}
''')
        (work / 'case.cc').write_text(SOURCE)
        result = subprocess.run([shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-I' + str(work), '-I' + str(ROOT / 'artdcevm/src/main/cpp'), '-I' + str(include),
            '-I' + str(include / platform), '-I' + str(ROOT / 'third_party/jvmti'), str(work / 'case.cc'), '-o', str(work / 'case')], capture_output=True)
        (out / 'compile.log').write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Constructor host compile failed; inspect ignored evidence')
        cases = [*range(20), 20, 21, 22, 24, 25, 26, 30, 31, 32]
        for case in cases:
            result = subprocess.run([str(work / 'case'), str(case)], capture_output=True)
            (out / f'case-{case}.log').write_bytes(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'Constructor host case {case} failed; inspect ignored evidence')
    print('Constructor history: ' + str(len(cases)) + ' production guard/lifecycle cases PASS')
    print('Evidence: ' + str(out.relative_to(ROOT)))


if __name__ == '__main__':
    main()

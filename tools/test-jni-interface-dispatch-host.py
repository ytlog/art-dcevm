#!/usr/bin/env python3
"""Compile the production post-CheckJNI receiver guard and exercise both JNI ABIs."""
from pathlib import Path
import runpy,shutil,subprocess,tempfile
from evidence_paths import new_evidence_directory
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''#include <jni.h>
#include "jvmti.h"
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include "platform/common/jni_interface_dispatch.h"
#include "platform/abi/arm64/common/jni_invoke.h"
using Value=artdcevm::abi::arm64::JniInvokeValue;
using CallA=Value(*)(const void*,jobject,jmethodID,const jvalue*);
using CallV=Value(*)(const void*,jobject,jmethodID,va_list);
void* hookA=nullptr;void* hookV=nullptr;void* directA=nullptr;void* directV=nullptr;
jmethodID alias=nullptr;int aliasLookups=0;
namespace artdcevm::member_mapping { jmethodID CopiedJniTarget(jmethodID id) noexcept {aliasLookups++;return alias?alias:id;} }
int mode=0,hooks=0,unhooks=0,originals=0,lookups=0,locals=0,deletes=0,instances=0;
bool failHook=false,failUnhook=false,pending=false,lookupFail=false,interfaceFail=false,envFail=false,compatible=true,interface=true;
std::string exception;
JNIEnv* nativeEnv;JavaVM machine; jvmtiEnv environment; jvalue operands[4]{};
void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void CheckV(va_list args){va_list copy;va_copy(copy,args);Require(va_arg(copy,int)==37&&va_arg(copy,long long)==0x123456789LL&&va_arg(copy,double)==2.5&&va_arg(copy,jobject)==reinterpret_cast<jobject>(56),"V arguments changed");va_end(copy);}
Value OriginalA(const void* soa,jobject,jmethodID,const jvalue* args){Require(soa==reinterpret_cast<void*>(77)&&args==operands&&args[0].i==37&&args[1].j==0x123456789LL&&args[2].d==2.5&&args[3].l==reinterpret_cast<jobject>(56),"A operands changed");originals++;return Value{0xc0decafe};}
Value OriginalV(const void* soa,jobject,jmethodID,va_list args){CheckV(args);Require(soa==reinterpret_cast<void*>(77),"V soa changed");originals++;return Value{0xc0decafe};}
Value DirectA(const void* soa,jobject,jmethodID id,const jvalue* args){Require(soa==reinterpret_cast<void*>(77)&&args==operands,"direct A operands changed");return Value{reinterpret_cast<uintptr_t>(id)};}
Value DirectV(const void* soa,jobject,jmethodID id,va_list args){CheckV(args);Require(soa==reinterpret_cast<void*>(77),"direct V soa changed");return Value{reinterpret_cast<uintptr_t>(id)};}
void* Current(){return reinterpret_cast<void*>(88);}
void Throw(void* self,const char* type,const char*){Require(self==Current(),"wrong thread");pending=true;exception=type;}
extern "C" void* shadowhook_dlopen(const char*){return reinterpret_cast<void*>(1);}
extern "C" void shadowhook_dlclose(void*){}
extern "C" void* shadowhook_dlsym(void*,const char* name){
 std::string n=name;
 if(n.find("InvokeVirtualOrInterfaceWithVarArgs")!=std::string::npos)return mode==1?nullptr:reinterpret_cast<void*>(OriginalV);
 if(n.find("InvokeVirtualOrInterfaceWithJValues")!=std::string::npos)return mode==2?nullptr:reinterpret_cast<void*>(OriginalA);
 if(n.find("InvokeWithVarArgs")!=std::string::npos)return mode==8?nullptr:reinterpret_cast<void*>(DirectV);
 if(n.find("InvokeWithJValues")!=std::string::npos)return mode==9?nullptr:reinterpret_cast<void*>(DirectA);
 if(n.find("CurrentFromGdb")!=std::string::npos || n.find("CurrentFrom")!=std::string::npos || n.find("CurrentEv")!=std::string::npos)return mode==7?nullptr:reinterpret_cast<void*>(Current);
 if(n.find("ThrowNewException")!=std::string::npos)return mode==6?nullptr:reinterpret_cast<void*>(Throw);
 return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
extern "C" void* shadowhook_hook_sym_addr(void* target,void* replacement,void** original){
 hooks++;
 if(failHook && ((mode==3&&target==reinterpret_cast<void*>(OriginalV)) || ((mode==4||mode==5)&&target==reinterpret_cast<void*>(OriginalA)) || (mode==10&&target==reinterpret_cast<void*>(DirectV)) || (mode==11&&target==reinterpret_cast<void*>(DirectA))))return nullptr;
 *original=target;if(target==reinterpret_cast<void*>(OriginalV))hookV=replacement;else if(target==reinterpret_cast<void*>(OriginalA))hookA=replacement;else if(target==reinterpret_cast<void*>(DirectV))directV=replacement;else directA=replacement;
 return target;
}
extern "C" int shadowhook_unhook(void*){unhooks++;return failUnhook?-1:0;}
jint GetEnv(JavaVM*,void** out,jint version){Require(version==JNI_VERSION_1_6,"wrong JNI version");*out=nativeEnv;return envFail?JNI_ERR:JNI_OK;}
jboolean Pending(JNIEnv*){return pending;}
void Delete(JNIEnv*,jobject){deletes++;}
jboolean Instance(JNIEnv*,jobject,jclass){instances++;return compatible;}
jvmtiError Declaring(jvmtiEnv* env,jmethodID,jclass* out){Require(env==&environment,"borrowed caller environment");lookups++;if(lookupFail)return JVMTI_ERROR_INVALID_METHODID;*out=reinterpret_cast<jclass>(20);locals++;return JVMTI_ERROR_NONE;}
jvmtiError IsInterface(jvmtiEnv*,jclass,jboolean* out){*out=interface?JNI_TRUE:JNI_FALSE;return interfaceFail?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Modifiers(jvmtiEnv*,jclass,jint*){throw std::runtime_error("receiver classification queried unrelated class modifiers");}
Value RunDirectV(jobject receiver,jmethodID method,...){va_list args;va_start(args,method);Value result=reinterpret_cast<CallV>(directV)(reinterpret_cast<void*>(77),receiver,method,args);va_end(args);return result;}
Value RunV(jobject receiver,jmethodID method,...){va_list args;va_start(args,method);Value result=reinterpret_cast<CallV>(hookV)(reinterpret_cast<void*>(77),receiver,method,args);va_end(args);return result;}
int main(int argc,char** argv){try{
 mode=argc>1?std::stoi(argv[1]):0;operands[0].i=37;operands[1].j=0x123456789LL;operands[2].d=2.5;operands[3].l=reinterpret_cast<jobject>(56);
 JNINativeInterface_ jni{};jni.ExceptionCheck=Pending;jni.DeleteLocalRef=Delete;jni.IsInstanceOf=Instance;JNIEnv native{&jni};nativeEnv=&native;
 JNIInvokeInterface_ vm{};vm.GetEnv=GetEnv;machine.functions=&vm;
 jvmtiInterface_1 table{};table.GetMethodDeclaringClass=Declaring;table.IsInterface=IsInterface;table.GetClassModifiers=Modifiers;environment.functions=&table;
 namespace guard=artdcevm::jni_interface_dispatch;
 Require(!guard::Enable()&&!guard::Prepare(),"unconfigured service available");
 guard::Configure(&machine,&environment);
 failHook=(mode>=3&&mode<=5)||mode==10||mode==11;failUnhook=mode==5;
 bool unavailable=mode>=1&&mode<=11;
 Require(guard::Prepare()!=unavailable,"wrong preparation availability");
 if(unavailable){Require(!guard::Enable(),"partial hook activated");failHook=false;failUnhook=false;mode=0;Require(guard::Prepare(),"retry failed");}
 auto callA=[&](jobject object,jmethodID id){return reinterpret_cast<CallA>(hookA)(reinterpret_cast<void*>(77),object,id,operands);};
 jobject object=reinterpret_cast<jobject>(30);jmethodID id=reinterpret_cast<jmethodID>(3);
 Require(callA(object,id).bits==0xc0decafe&&lookups==0,"disabled hook inspected ID");
 Require(reinterpret_cast<CallA>(directA)(reinterpret_cast<void*>(77),object,id,operands).bits==3&&aliasLookups==0,"disabled direct hook inspected ID");
 Require(guard::Enable(),"activation failed");int installed=hooks;Require(guard::Prepare()&&hooks==installed,"reinstalled live hooks");
 for(int tag=0;tag<2;tag++){
  id=reinterpret_cast<jmethodID>(tag?3:2);compatible=true;interface=true;
  Require(callA(object,id).bits==0xc0decafe&&!pending,"valid A changed");
  Require(RunV(object,id,37,0x123456789LL,2.5,reinterpret_cast<jobject>(56)).bits==0xc0decafe&&!pending,"valid V changed");
  compatible=false;int old=originals;
  Require(callA(object,id).bits==0&&pending&&exception=="Ljava/lang/IncompatibleClassChangeError;"&&originals==old,"bad A forwarded");pending=false;
  Require(RunV(object,id,37,0x123456789LL,2.5,reinterpret_cast<jobject>(56)).bits==0&&pending&&exception=="Ljava/lang/IncompatibleClassChangeError;"&&originals==old,"bad V forwarded");pending=false;
  interface=false;int before=instances;Require(callA(object,id).bits==0xc0decafe&&!pending&&instances==before,"own class checked as interface");
 }
 alias=reinterpret_cast<jmethodID>(42);
 Require(reinterpret_cast<CallA>(directA)(reinterpret_cast<void*>(77),object,id,operands).bits==42,"direct A lost committed alias");
 Require(RunDirectV(object,id,37,0x123456789LL,2.5,reinterpret_cast<jobject>(56)).bits==42,"direct V lost committed alias");alias=nullptr;
 Require(reinterpret_cast<CallA>(directA)(reinterpret_cast<void*>(77),object,id,operands).bits==reinterpret_cast<uintptr_t>(id),"unmapped ID changed");
 int before=lookups;pending=true;callA(object,id);Require(lookups==before,"pending exception inspected");pending=false;
 callA(nullptr,id);callA(object,nullptr);Require(lookups==before,"null ordering changed");
 lookupFail=true;callA(object,id);Require(pending&&exception=="Ljava/lang/InternalError;","lookup failure fell through");pending=false;lookupFail=false;
 interfaceFail=true;callA(object,id);Require(pending&&exception=="Ljava/lang/InternalError;","interface query failure fell through");pending=false;interfaceFail=false;
 envFail=true;before=lookups;callA(object,id);Require(pending&&lookups==before&&exception=="Ljava/lang/InternalError;","missing environment fell through");
 Require(locals==deletes,"declaring local roots leaked");
 std::cout<<"JNI receiver guard PASS\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
'''
def main():
    out=new_evidence_directory(ROOT/'build/evidence/host/jni-interface-dispatch')
    helper=runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))
    inc=helper['java_home'](None)/'include';platform='darwin' if (inc/'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        work=Path(tmp);(work/'shadowhook.h').write_text('#pragma once\nextern "C" { void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*); void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*); void* shadowhook_hook_sym_addr(void*,void*,void**); int shadowhook_unhook(void*); }\n')
        source=work/'test.cc';source.write_text(SOURCE);native=ROOT/'artdcevm/src/main/cpp';binary=work/'test'
        command=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(work),'-I'+str(native),'-I'+str(inc),'-I'+str(inc/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),str(native/'platform/common/jni_interface_dispatch.cc'),'-o',str(binary)]
        r=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('JNI guard compilation failed; inspect ignored evidence')
        for mode in range(12):
            r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,timeout=30);(out/f'case-{mode}.log').write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError('JNI receiver case failed; inspect ignored evidence')
            print('JNI receiver case',mode,'PASS')
    print('Evidence: '+str(out.relative_to(ROOT)))
if __name__=='__main__':main()

#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/android13/device_compiler_diagnostics_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
namespace g=artdcevm::android13;namespace a=g::compiler_diagnostics_abi;
namespace {
a::RuntimePolicy policy{};a::Options options{};a::Compiler compiler{};a::Jit jit{};void* compiler_ptr=&compiler;
void* jit_ptr=&jit;bool inspect,deopt,all_deopt,compiled=true,suspended,baseline=false,activate=true;
size_t compilation_calls,visible_calls,invalidate_calls,starts,stops;
int thread;void* runtime=reinterpret_cast<void*>(42);void* cache=reinterpret_cast<void*>(43);
g::validation_abi::ClassPrefix declaring{};const void* aot=reinterpret_cast<void*>(44);
void SuspendCtor(a::Suspend* s){assert(!suspended);s->was_on=true;suspended=true;}
void SuspendDtor(a::Suspend* s){assert(suspended&&s->was_on);suspended=false;}
void Stop(void* p){assert(p==&jit);++stops;}void Start(void* p){assert(p==&jit);++starts;}
void Debug(void* p,bool value){assert(p==runtime);policy.java_debuggable=value;}
void* Callbacks(void* p){assert(p==runtime);return p;}
bool Inspected(void*,void*){return inspect;}bool Deoptimized(void*,void*){return deopt;}
void Visible(void* p,void* self,bool wait){assert(p==reinterpret_cast<void*>(45)&&self==&thread&&wait);++visible_calls;}
bool Compile(void* p,void*,void* self,uint32_t kind,bool prejit){assert(suspended&&p==&jit&&self==&thread&&kind==2&&!prejit);++compilation_calls;return compiled;}
const void* Oat(void*,size_t size){assert(size==8);return aot;}
void Initialize(void* p,void* m,const void* code){assert(p==&policy);static_cast<g::art_method_abi::ArtMethod*>(m)->entry_point=uintptr_t(!policy.java_debuggable&&activate?code:nullptr);}
void Invalidate(void* p){assert(p==cache);++invalidate_calls;}
void* Root(void*){return &declaring;}
bool Baseline(const void* p){assert(p==&compiler);return baseline;}
void* Original(void* callee,void* outer,void* invoke){assert(invoke==reinterpret_cast<void*>(123));static_cast<a::Graph*>(callee)->method=nullptr;static_cast<a::Graph*>(outer)->method=nullptr;return invoke;}
class Hooks:public artdcevm::HookEngine {
 public:bool Initialize(std::string*)override{return true;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  std::string s=name;
#define MAP(part,fn) if(s.find(part)!=s.npos)return reinterpret_cast<void*>(fn)
  MAP("ScopedJitSuspendC2",SuspendCtor);MAP("ScopedJitSuspendD2",SuspendDtor);MAP("Jit4Stop",Stop);MAP("Jit5Start",Start);
  MAP("SetJavaDebuggable",Debug);MAP("GetRuntimeCallbacks",Callbacks);MAP("IsMethodBeingInspected",Inspected);MAP("IsDeoptimized",Deoptimized);
  MAP("MakeInitializedClasses",Visible);MAP("CompileMethod",Compile);MAP("InitializeMethodsCode",::Initialize);MAP("GetOatMethodQuickCode",Oat);
  MAP("jit_compiler_E",&compiler_ptr);MAP("InvalidateAllCompiledCode",Invalidate);MAP("artReadBarrierForRootSlow",Root);
#undef MAP
  return nullptr;
 }
};
}
namespace artdcevm::android13 {
void* RuntimeInstance(){return runtime;}void* RuntimeJit(){return jit_ptr;}void* RuntimeJitCodeCache(){return cache;}void* RuntimeInstrumentation(){return &policy;}void* RuntimeClassLinker(){return reinterpret_cast<void*>(45);}
bool DiagnosticAllMethodsDeoptimized(){return all_deopt;}
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(&thread),supported_(true),transitioned_(false){}
NativeObjectAccess::~NativeObjectAccess(){assert(self_==&thread&&!transitioned_);}
void* NativeObjectAccess::Decode(jobject object)const{return object;}
}
extern "C" int ArtDcevmSourceProbeCompilerDiagnostics(JNIEnv*,int (*state)(),bool (*debug)()){assert(state()==5&&!debug());return 0;}
int main(){
 compiler.options=&options;options.inline_max=10;options.debuggable=true;void* vtable[7]{};vtable[6]=reinterpret_cast<void*>(Baseline);compiler.vtable=vtable;jit.pool=&thread;
 Hooks hooks;std::string error;JNINativeInterface_ functions{};functions.ExceptionCheck=[](JNIEnv*)->jboolean{return false;};JNIEnv env{&functions};void* self=dlopen(nullptr,RTLD_NOW);assert(g::ConfigureDeviceCompilerDiagnostics(hooks,self,&env,&error));
 g::art_method_abi::ArtMethod method{},other{};g::member_decode_abi::Executable executable{},second{};executable.art_method=uintptr_t(&method);second.art_method=uintptr_t(&other);jobject ref=reinterpret_cast<jobject>(&executable),ref2=reinterpret_cast<jobject>(&second);
 assert(g::DiagnosticJitCompilerOptionsState()==5);baseline=true;options.baseline=true;assert(g::DiagnosticJitCompilerOptionsState()==15);baseline=false;options.baseline=false;
 jit_ptr=nullptr;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-1&&g::DiagnosticJitCompilerOptionsState()==-1);jit_ptr=&jit;
 policy.java_debuggable=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2);policy.java_debuggable=false;all_deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2);all_deopt=false;
 inspect=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-3);inspect=false;
 method.access_flags=a::kDontCompile;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-4);method.access_flags|=a::kPreCompiled;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1);method.access_flags=a::kIntrinsic|a::kDontCompile;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1);method.access_flags=0;
 deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-5);deopt=false;declaring.access_flags=0x40000;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-6);declaring.access_flags=0;compiled=false;assert(g::DiagnosticForceOptimizedJit(&env,ref)==0);compiled=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1&&visible_calls==compilation_calls);
 assert(g::DiagnosticJitInlineExperiment(&env,true)==-2);policy.java_debuggable=true;assert(g::DiagnosticJitInlineExperiment(&env,true)==1&&!options.debuggable&&!policy.java_debuggable);assert(g::DiagnosticJitInlineExperiment(&env,false)==1&&options.debuggable&&policy.java_debuggable&&invalidate_calls==1);
 compiler_ptr=nullptr;assert(g::DiagnosticJitInlineExperiment(&env,true)==-1);compiler_ptr=&compiler;
 all_deopt=true;assert(g::DiagnosticAotExperiment(&env,ref,true)==-3);all_deopt=false;aot=nullptr;assert(g::DiagnosticAotExperiment(&env,ref,true)==-2);aot=reinterpret_cast<void*>(44);
 assert(g::DiagnosticAotExperiment(&env,ref,true)==1&&!policy.java_debuggable);assert(g::DiagnosticAotExperiment(&env,ref2,true)==1);assert(g::DiagnosticAotExperiment(&env,ref,false)==1&&policy.java_debuggable);activate=false;assert(g::DiagnosticAotExperiment(&env,ref,true)==0&&policy.java_debuggable);activate=true;
 assert(g::DiagnosticPauseJitForGraphHook(true)==1&&stops==1);assert(g::DiagnosticPauseJitForGraphHook(false)==1&&starts==1);jit.pool=nullptr;assert(g::DiagnosticPauseJitForGraphHook(true)==-1);jit.pool=&thread;
 a::Graph inner{},outer{};inner.method=&other;outer.method=&method;assert(g::DiagnosticHGraphInlineInto(&inner,&outer,nullptr)==nullptr);
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,ref,ref2,reinterpret_cast<void*>(Original))==1);assert(g::DiagnosticHGraphInlineInto(&inner,&outer,reinterpret_cast<void*>(123))==reinterpret_cast<void*>(123)&&g::DiagnosticGraphInlineHits()==1);
 inner.method=&method;outer.method=&other;g::DiagnosticHGraphInlineInto(&inner,&outer,reinterpret_cast<void*>(123));assert(g::DiagnosticGraphInlineHits()==1);
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,nullptr,nullptr,nullptr)==1&&g::DiagnosticHGraphInlineInto(nullptr,nullptr,nullptr)==nullptr);assert(!suspended);
 std::cout<<"compiler options, optimized JIT preconditions/compile, policy restoration, AOT activation, graph original/target accounting: passed\n";
}

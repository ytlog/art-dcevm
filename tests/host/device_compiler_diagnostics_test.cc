#include "device_profile_fixture.h"
#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/common/jit_service.h"
#include "platform/abi/arm64/android13/device_compiler_diagnostics_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/abi/arm64/android13/member_decode_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
namespace g=artdcevm::android13;namespace a=g::compiler_diagnostics_abi;
namespace {
a::RuntimePolicy policy{};a::Options options{};a::Compiler compiler{};a::Jit jit{};void* compiler_ptr=&compiler;
void* jit_ptr=&jit;bool inspect,deopt,all_deopt,compiled=true,suspended,baseline=false,activate=true;
size_t compilation_calls,visible_calls,invalidate_calls,starts,stops;
std::vector<bool> runtime_changes;bool emit_inline_graph=false,expect_restored_exit=false;
g::art_method_abi::ArtMethod* inline_callee=nullptr;
struct Inliner {void* vtable;a::Graph* graph;void* stats;const char* name;a::Graph* outermost;void* outer_unit;void* caller_unit;void* codegen;size_t registers,instructions;void* parent;size_t depth,budget;void* inline_stats;};
struct List {void* first;void* last;};struct Block {a::Graph* graph;a::ArenaVector predecessors,successors;List instructions,phis;};
struct Instruction {void* vtable;void* previous;void* next;void* block;uint32_t dex_pc;int32_t id,ssa;uint32_t packed;void* uses;void* env_uses;void* environment;void* locations;void* interval;size_t lifetime;uint64_t effects;void* type_handle;};
struct MethodRef {void* dex;uint32_t index;};struct Invoke {Instruction instruction;a::ArenaVector inputs;uint32_t arguments;void* resolved;MethodRef method,reference;uint32_t intrinsic,optimizations;};
bool (*run_proxy)(void*)=nullptr;bool install_failure=false,remove_failure=false;
unsigned original_runs=0,inline_calls=0,budget_calls=0;bool mutate_inlining=true,in_nested_pass=false;
bool OriginalRun(void*){++original_runs;return false;}
void Budget(void* raw){auto* inliner=static_cast<Inliner*>(raw);assert(inliner->graph->debuggable&&options.debuggable);assert(inliner->instructions==(in_nested_pass?77:4));++budget_calls;inliner->budget=100;}
bool TryInline(void* raw,void* invoke,bool* throws){auto* inliner=static_cast<Inliner*>(raw);assert(inliner->graph->debuggable&&options.debuggable&&policy.java_debuggable);++inline_calls;
 auto* instruction=static_cast<Invoke*>(invoke);if(instruction->arguments==9){*throws=true;return false;}
 if(!in_nested_pass){
  // A recursive callee remains debug from SSA construction, and its inherited
  // instruction budget must not be reset by the owned Run implementation.
  a::Graph nested{};nested.debuggable=true;Block block{};Invoke child{};child.instruction.packed=41<<2;child.arguments=9;
  block.instructions={&child,&child};void* blocks[]={&block};nested.reverse.values={blocks,blocks+1,blocks+1};
  Inliner recursive{};recursive.graph=&nested;recursive.outermost=inliner->graph;recursive.instructions=77;
  in_nested_pass=true;assert(run_proxy(&recursive)&&recursive.instructions==77&&nested.debuggable);in_nested_pass=false;
 }
 if(mutate_inlining){instruction->instruction.next=nullptr;inliner->graph->reverse.values.end=static_cast<void**>(inliner->graph->reverse.values.begin)+3;}
 return true;}
uint32_t compiled_code[4]{0x1234,0x5678,0x9012,0x3456};
int thread;void* runtime=reinterpret_cast<void*>(42);void* cache=reinterpret_cast<void*>(43);
g::validation_abi::ClassPrefix declaring{};const void* aot=reinterpret_cast<void*>(44);
void SuspendCtor(a::Suspend* s){assert(!suspended);s->was_on=true;suspended=true;}
void SuspendDtor(a::Suspend* s){assert(suspended&&s->was_on&&(!expect_restored_exit||policy.java_debuggable));suspended=false;}
void Stop(void* p){assert(p==&jit);++stops;}void Start(void* p){assert(p==&jit);++starts;}
void Debug(void* p,bool value){assert(p==runtime);policy.java_debuggable=value;runtime_changes.push_back(value);}
void* Callbacks(void* p){assert(p==runtime);return p;}
bool Inspected(void*,void*){return inspect;}bool Deoptimized(void*,void*){return deopt;}
void Visible(void* p,void* self,bool wait){assert(p==reinterpret_cast<void*>(45)&&self==&thread&&wait);++visible_calls;}
bool Compile(void* p,void* method,void* self,artdcevm::jit_service::CompilationKind12 kind,bool prejit){
 assert(suspended&&p==&jit&&self==&thread&&kind==artdcevm::jit_service::CompilationKind12::kOptimized&&!prejit);++compilation_calls;
 if(emit_inline_graph){assert(options.debuggable&&policy.java_debuggable&&run_proxy);a::Graph inner{},outer{};inner.debuggable=outer.debuggable=true;inner.method=inline_callee;outer.method=method;
  Instruction entry{};Invoke call{},intrinsic{},second{};Instruction ordinary{};call.instruction.packed=(38<<2)|(3<<10)|1;intrinsic.instruction.packed=43<<2;intrinsic.intrinsic=1;second.instruction.packed=40<<2;second.arguments=9;ordinary.packed=1<<2;
  call.instruction.next=&intrinsic;intrinsic.instruction.next=&second;second.instruction.next=&ordinary;
  Block start{},body{},added{};start.instructions={&entry,&entry};body.instructions={&call,&ordinary};added.instructions={&call,&call};void* blocks[]={&start,&body,&added};outer.entry=&start;outer.reverse.values={blocks,blocks+2,blocks+3};
  Inliner inliner{};inliner.graph=inliner.outermost=&outer;auto before_inline=inline_calls;assert(run_proxy(&inliner)&&inline_calls==before_inline+3&&outer.debuggable&&inliner.instructions==4);
  assert(g::DiagnosticHGraphInlineInto(&inner,&outer,reinterpret_cast<void*>(123))==reinterpret_cast<void*>(123));}
 if(compiled)static_cast<g::art_method_abi::ArtMethod*>(method)->entry_point=uintptr_t(compiled_code);
 return compiled;
}
const void* Oat(void*,size_t size){assert(size==8);return aot;}
void Initialize(void* p,void* m,const void* code){assert(p==&policy);static_cast<g::art_method_abi::ArtMethod*>(m)->entry_point=uintptr_t(!policy.java_debuggable&&activate?code:nullptr);}
void Invalidate(void* p){assert(p==cache);++invalidate_calls;}
void* Root(void*){return &declaring;}
bool Baseline(const void* p){assert(p==&compiler);return baseline;}
void* Original(void* callee,void* outer,void* invoke){assert(invoke==reinterpret_cast<void*>(123));static_cast<a::Graph*>(callee)->method=nullptr;static_cast<a::Graph*>(outer)->method=nullptr;return invoke;}
class Hooks:public artdcevm::HookEngine {
 public:bool Initialize(std::string*)override{return true;}
 void* Install(void* target,void* proxy,void** original,std::string*)override{assert(suspended&&target==reinterpret_cast<void*>(OriginalRun));if(install_failure)return nullptr;*original=target;run_proxy=reinterpret_cast<bool(*)(void*)>(proxy);return this;}
 bool Remove(void* token,std::string*)override{assert(suspended&&token==this);if(remove_failure)return false;run_proxy=nullptr;return true;}
 void* FindSymbol(const char*,const char* name)override{
  std::string s=name;
  if(s=="_ZN3art8HInliner3RunEv")return reinterpret_cast<void*>(OriginalRun);
  if(s=="_ZN3art8HInliner9TryInlineEPNS_7HInvokeEPb")return reinterpret_cast<void*>(TryInline);
  if(s=="_ZN3art8HInliner20UpdateInliningBudgetEv")return reinterpret_cast<void*>(Budget);
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
NativeObjectAccess::~NativeObjectAccess(){assert(self_==&thread&&!transitioned_&&(!expect_restored_exit||policy.java_debuggable));}
void* NativeObjectAccess::Decode(jobject object)const{return object;}
}
int main(){ auto profile=MatchedProfileForTest();
 compiler.options=&options;options.inline_max=10;options.debuggable=true;void* vtable[7]{};vtable[6]=reinterpret_cast<void*>(Baseline);compiler.vtable=vtable;jit.pool=&thread;
 Hooks hooks;std::string error;JNINativeInterface_ functions{};functions.ExceptionCheck=[](JNIEnv*)->jboolean{return false;};JNIEnv env{&functions};assert(!g::ConfigureDeviceCompilerDiagnostics(hooks,g::DeviceProfile{},&error));assert(g::ConfigureDeviceCompilerDiagnostics(hooks,profile,&error));assert(g::DiagnosticJitCompilerOptionsState()==5);
 g::art_method_abi::ArtMethod method{},other{};g::member_decode_abi::Executable executable{},second{};executable.art_method=uintptr_t(&method);second.art_method=uintptr_t(&other);jobject ref=reinterpret_cast<jobject>(&executable),ref2=reinterpret_cast<jobject>(&second);
 assert(g::DiagnosticJitCompilerOptionsState()==5);baseline=true;options.baseline=true;assert(g::DiagnosticJitCompilerOptionsState()==15);baseline=false;options.baseline=false;
 jit_ptr=nullptr;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-1&&g::DiagnosticJitCompilerOptionsState()==-1);jit_ptr=&jit;
 policy.java_debuggable=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2);policy.java_debuggable=false;all_deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2);all_deopt=false;
 inspect=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-3);inspect=false;
 method.access_flags=a::kDontCompile;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-4);method.access_flags|=a::kPreCompiled;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1);method.access_flags=a::kIntrinsic|a::kDontCompile;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1);method.access_flags=0;
 deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-5);deopt=false;declaring.access_flags=0x40000;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-6);declaring.access_flags=0;compiled=false;assert(g::DiagnosticForceOptimizedJit(&env,ref)==0);compiled=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==1&&visible_calls==compilation_calls);
 // An ineligible non-debuggable runtime is never silently changed to true.
 assert(g::DiagnosticJitInlineExperiment(&env,true)==-2&&!policy.java_debuggable&&options.debuggable);
 assert(g::DiagnosticJitInlineExperiment(&env,false)==1&&!policy.java_debuggable&&options.debuggable&&invalidate_calls==0);
 policy.java_debuggable=true;install_failure=true;assert(g::DiagnosticJitInlineExperiment(&env,true)==0&&options.debuggable&&policy.java_debuggable);install_failure=false;assert(g::DiagnosticJitInlineExperiment(&env,true)==1&&options.debuggable&&policy.java_debuggable);
 assert(g::DiagnosticJitInlineExperiment(&env,true)==-2&&options.debuggable&&policy.java_debuggable);
 expect_restored_exit=true;
 const auto policy_changes=runtime_changes.size();const auto invalidations=invalidate_calls;
 all_deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2&&policy.java_debuggable);all_deopt=false;
 inspect=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-3&&policy.java_debuggable);inspect=false;
 method.access_flags=a::kDontCompile;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-4&&policy.java_debuggable);method.access_flags=0;
 deopt=true;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-5&&policy.java_debuggable);deopt=false;
 declaring.access_flags=0x40000;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-6&&policy.java_debuggable);declaring.access_flags=0;
 assert(runtime_changes.size()==policy_changes);
 // Named compile retains debug policy and the requested optimized
 // kind; the real production graph proxy sees its caller/callee identities.
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,ref,ref2,reinterpret_cast<void*>(Original))==1);
 inline_callee=&other;emit_inline_graph=true;
 compiled=false;assert(g::DiagnosticForceOptimizedJit(&env,ref)==0&&policy.java_debuggable&&options.debuggable);compiled=true;

 for(int i=0;i<2;++i){assert(g::DiagnosticForceOptimizedJit(&env,ref)==1&&policy.java_debuggable&&options.debuggable);
  assert(method.entry_point==uintptr_t(compiled_code)&&invalidate_calls==invalidations);}
 emit_inline_graph=false;assert(g::DiagnosticGraphInlineHits()==3);
 assert(runtime_changes.size()==policy_changes);
 assert(budget_calls==6&&inline_calls==9);
 Inliner unused{};const auto pass_through=original_runs;assert(!run_proxy(&unused)&&original_runs==pass_through+1);
 compiler_ptr=nullptr;assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2&&policy.java_debuggable);compiler_ptr=&compiler;
 remove_failure=true;assert(g::DiagnosticJitInlineExperiment(&env,false)==0&&options.debuggable&&policy.java_debuggable&&invalidate_calls==0);remove_failure=false;
 assert(g::DiagnosticJitInlineExperiment(&env,false)==1&&options.debuggable&&policy.java_debuggable&&invalidate_calls==1);
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,nullptr,nullptr,nullptr)==1);
 expect_restored_exit=false;
 assert(g::DiagnosticForceOptimizedJit(&env,ref)==-2);
 compiler_ptr=nullptr;assert(g::DiagnosticJitInlineExperiment(&env,true)==-1);compiler_ptr=&compiler;
 all_deopt=true;assert(g::DiagnosticAotExperiment(&env,ref,true)==-3);all_deopt=false;aot=nullptr;assert(g::DiagnosticAotExperiment(&env,ref,true)==-2);aot=reinterpret_cast<void*>(44);
 // Multiple activations preserve the first policy; a failed subsequent target
 // must not end the session or change the currently active non-debuggable mode.
 assert(g::DiagnosticAotExperiment(&env,ref,true)==1&&!policy.java_debuggable);
 assert(g::DiagnosticAotExperiment(&env,ref2,true)==1&&!policy.java_debuggable);
 activate=false;assert(g::DiagnosticAotExperiment(&env,ref2,true)==0&&!policy.java_debuggable);activate=true;
 assert(g::DiagnosticAotExperiment(&env,ref,false)==1&&policy.java_debuggable);
 const auto after_restore=runtime_changes.size();
 assert(g::DiagnosticAotExperiment(&env,ref2,false)==1&&policy.java_debuggable&&runtime_changes.size()==after_restore);
 activate=false;assert(g::DiagnosticAotExperiment(&env,ref,true)==0&&policy.java_debuggable);activate=true;
 // A non-debuggable compiler must never acquire the runtime's debug flag via
 // diagnostic enable, failed activation, or an unmatched disable.
 policy.java_debuggable=false;options.debuggable=false;
 const auto before_reject=runtime_changes.size();
 assert(g::DiagnosticAotExperiment(&env,ref,true)==-2&&!policy.java_debuggable);
 assert(g::DiagnosticAotExperiment(&env,ref,false)==1&&!policy.java_debuggable&&runtime_changes.size()==before_reject);
 compiler_ptr=nullptr;assert(g::DiagnosticAotExperiment(&env,ref,true)==-2&&!policy.java_debuggable);compiler_ptr=&compiler;
 // Already-debug compiler options permit temporary runtime policy changes,
 // but restoring an originally false runtime must keep it false.
 options.debuggable=true;
 assert(g::DiagnosticAotExperiment(&env,ref,true)==1&&!policy.java_debuggable);
 assert(g::DiagnosticAotExperiment(&env,ref,false)==1&&!policy.java_debuggable);
 activate=false;assert(g::DiagnosticAotExperiment(&env,ref,true)==0&&!policy.java_debuggable);activate=true;
 policy.java_debuggable=true;
 assert(g::DiagnosticPauseJitForGraphHook(true)==1&&stops==1);assert(g::DiagnosticPauseJitForGraphHook(false)==1&&starts==1);jit.pool=nullptr;assert(g::DiagnosticPauseJitForGraphHook(true)==-1);jit.pool=&thread;
 a::Graph inner{},outer{};inner.method=&other;outer.method=&method;assert(g::DiagnosticHGraphInlineInto(&inner,&outer,nullptr)==nullptr);
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,ref,ref2,reinterpret_cast<void*>(Original))==1);assert(g::DiagnosticHGraphInlineInto(&inner,&outer,reinterpret_cast<void*>(123))==reinterpret_cast<void*>(123)&&g::DiagnosticGraphInlineHits()==1);
 inner.method=&method;outer.method=&other;g::DiagnosticHGraphInlineInto(&inner,&outer,reinterpret_cast<void*>(123));assert(g::DiagnosticGraphInlineHits()==1);
 assert(g::DiagnosticConfigureGraphInlineProbe(&env,nullptr,nullptr,nullptr)==1&&g::DiagnosticHGraphInlineInto(nullptr,nullptr,nullptr)==nullptr);assert(!suspended);
 std::cout<<"compiler options, optimized JIT preconditions/compile, policy restoration, AOT activation, graph original/target accounting: passed\n";
}

#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/abi/arm64/android13/device_compiler_diagnostics_abi.h"
#include "platform/android13/device_diagnostics.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/native_object_access.h"
#include "platform/abi/arm64/android13/member_decode_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/common/jit_service.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace artdcevm::android13 {
namespace {
namespace a=compiler_diagnostics_abi;
void (*suspend_ctor)(a::Suspend*),(*suspend_dtor)(a::Suspend*);
void (*stop)(void*),(*start)(void*),(*set_debuggable)(void*,bool);
void* (*callbacks)(void*);
bool (*inspected)(void*,void*),(*deoptimized)(void*,void*);
void (*visible)(void*,void*,bool),(*initialize)(void*,void*,const void*);
jit_service::Compiler compile;
const void* (*oat_code)(void*,size_t);
void (*invalidate)(void*);
void** compiler_slot;
using Inline=void* (*)(void*,void*,void*);
void* (*root_reader)(void*);
std::atomic<Inline> original{nullptr};std::atomic<void*> caller{nullptr},callee{nullptr};std::atomic<uint64_t> hits{0};
template<class T> T Read(const void* p,size_t off=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(v));return v;}
void Require(bool value){if(!value)std::abort();}
class Suspend {public:Suspend(){suspend_ctor(&storage);}~Suspend(){suspend_dtor(&storage);}private:a::Suspend storage{};};
void* Method(NativeObjectAccess& access,jobject executable){Require(access.Supported());return reinterpret_cast<void*>(Read<uint64_t>(access.Decode(executable),offsetof(member_decode_abi::Executable,art_method)));}
a::Options* Options(){auto* compiler=static_cast<a::Compiler*>(*compiler_slot);return compiler?compiler->options:nullptr;}
bool Debuggable(){return Read<bool>(RuntimeInstrumentation(),offsetof(a::RuntimePolicy,java_debuggable));}
// Fixed compiler/optimizing/{optimization,inliner,nodes}.h declarations.
// These borrowed prefixes never construct a platform STL object.
struct InlinerPrefix {
 void* vtable; a::Graph* graph; void* stats; const char* pass_name;
 a::Graph* outermost;const void* outer_unit;const void* caller_unit;void* codegen;
 size_t total_registers,total_instructions;const void* parent;size_t depth,budget;void* inline_stats;
};
struct InstructionList {void* first;void* last;};
struct BlockPrefix {a::Graph* graph;a::ArenaVector predecessors,successors;InstructionList instructions,phis;};
struct InstructionPrefix {
 void* vtable;void* previous;void* next;void* block;
 uint32_t dex_pc;int32_t id,ssa_index;uint32_t packed_fields;
 void* uses;void* environment_uses;void* environment;void* locations;void* interval;
 size_t lifetime;uint64_t side_effects;void* reference_type_handle;
};
struct MethodReference {const void* dex;uint32_t index;};
struct InvokePrefix {
 InstructionPrefix instruction;a::ArenaVector inputs;uint32_t arguments;void* resolved_method;
 MethodReference method,resolved;uint32_t intrinsic,optimizations;
};
static_assert(sizeof(InstructionPrefix)==112 && offsetof(InstructionPrefix,next)==16 && offsetof(InstructionPrefix,packed_fields)==44);
static_assert(offsetof(InvokePrefix,intrinsic)==192 && offsetof(BlockPrefix,instructions)==72);
static_assert(sizeof(InlinerPrefix)==112 && offsetof(InlinerPrefix,total_instructions)==72);
using InlinerRun=bool(*)(void*);using TryInline=bool(*)(void*,void*,bool*);
InlinerRun original_inliner_run=nullptr;TryInline try_inline=nullptr;void(*update_budget)(void*)=nullptr;
HookEngine* compiler_hooks=nullptr;void* inliner_target=nullptr;void* inliner_token=nullptr;
thread_local bool diagnostic_inline_compile=false;
// Keep the debug graph from construction through SSA, optimization, liveness,
// and code generation. Only this pass's no-inlining entry restriction changes.
bool DiagnosticInlinerRun(void* raw){
 if(!diagnostic_inline_compile)return original_inliner_run(raw);
 auto* options=Options();auto* inliner=static_cast<InlinerPrefix*>(raw);
 if(!options||options->compile_test)return original_inliner_run(raw);
 if(!options->inline_max)return false;
 auto* graph=inliner->graph;Require(graph&&graph->debuggable);
 auto first=static_cast<void**>(graph->reverse.values.begin),last=static_cast<void**>(graph->reverse.values.end);
 std::vector<void*> blocks(first,last);
 if(inliner->outermost==graph){size_t count=0;
  for(void* raw_block:blocks){if(raw_block==graph->entry)continue;auto* block=static_cast<BlockPrefix*>(raw_block);
   for(auto* instruction=static_cast<InstructionPrefix*>(block->instructions.first);instruction;instruction=static_cast<InstructionPrefix*>(instruction->next))++count;
  }inliner->total_instructions=count;
 }
 update_budget(raw);bool did_inline=false,did_set_always_throws=false;
 for(void* raw_block:blocks){auto* block=static_cast<BlockPrefix*>(raw_block);
  for(auto* instruction=static_cast<InstructionPrefix*>(block->instructions.first);instruction;){
   // TryInline mutates lists and adds blocks: preserve the original next and
   // snapshot RPO, exactly as device HInliner::Run does.
   auto* next=static_cast<InstructionPrefix*>(instruction->next);
   // nodes.h: instruction kind starts at bit 2; locked ARM-enabled enum has
   // >=129 kinds (8 bits). InvokeUnresolved..InvokeCustom are kinds 38..43.
   const uint32_t kind=(instruction->packed_fields>>2)&0xffu;
   if(kind>=38&&kind<=43&&static_cast<InvokePrefix*>(static_cast<void*>(instruction))->intrinsic==0)
    if(try_inline(raw,instruction,&did_set_always_throws))did_inline=true;
   instruction=next;
  }
 }
 return did_inline||did_set_always_throws;
}
// The isolated diagnostic caller serializes enable/compile/disable operations.
struct InlineExperiment {bool enabled=false,runtime_debuggable=false,compiler_debuggable=false;a::Options* options=nullptr;} inline_experiment;
// A session may activate several methods. Preserve the policy from its first
// successful activation rather than treating every subsequent enable as a new
// transition from the temporarily non-debuggable runtime.
struct AotExperiment {bool enabled=false,runtime_debuggable=false;} aot_experiment;
class ScopedInlineCompile {
 public:explicit ScopedInlineCompile(bool enabled):original_(diagnostic_inline_compile){diagnostic_inline_compile=enabled;}
 ~ScopedInlineCompile(){diagnostic_inline_compile=original_;}
 private:bool original_;
};
uint32_t Flags(void* method){return art_method_abi::ReadAccessFlags(method);}
// JitCompilerInterface: D1/D0, CompileMethod, TypesLoaded, GenerateDebugInfo,
// ParseCompilerOptions, IsBaselineCompiler. No device STL return is invoked.
bool BaselineInterface(void* compiler){auto** table=*static_cast<void***>(compiler);using Fn=bool (*)(const void*);return reinterpret_cast<Fn>(table[6])(compiler);}
}
int DiagnosticForceOptimizedJit(JNIEnv* env,jobject executable){
 auto* jit=RuntimeJit();if(!jit)return -1;Suspend suspend;NativeObjectAccess access(env);auto* method=Method(access,executable);
 if(DiagnosticAllMethodsDeoptimized())return -2;
 if(inline_experiment.enabled){
  if(Debuggable()!=inline_experiment.runtime_debuggable||Options()!=inline_experiment.options||Options()->debuggable!=inline_experiment.compiler_debuggable)return -2;
 }else if(Debuggable())return -2;
 if(inspected(callbacks(RuntimeInstance()),method))return -3;
 uint32_t flags=Flags(method);
 if(!(flags&a::kIntrinsic)&&!(flags&a::kPreCompiled)&&(flags&a::kDontCompile))return -4;
 if(deoptimized(RuntimeInstrumentation(),method))return -5;
 auto* klass=root_reader(method);
 if(Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,access_flags))&0x40000)return -6;
 ScopedInlineCompile policy(inline_experiment.enabled);
 visible(RuntimeClassLinker(),access.Self(),true);
 return compile.Compile(jit,method,access.Self(),jit_service::Kind::kOptimized)?1:0;
}
int DiagnosticJitInlineExperiment(JNIEnv* env,bool enable){
 auto* jit=RuntimeJit();if(!jit||!*compiler_slot)return -1;Suspend suspend;NativeObjectAccess access(env);Require(access.Supported());auto* options=Options();
 if(enable){if(inline_experiment.enabled||!options||!Debuggable()||!options->debuggable||DiagnosticAllMethodsDeoptimized())return -2;
 std::string error;
 inliner_token=compiler_hooks->Install(inliner_target,reinterpret_cast<void*>(DiagnosticInlinerRun),reinterpret_cast<void**>(&original_inliner_run),&error);
 if(!inliner_token)return 0;
 inline_experiment={true,Debuggable(),options->debuggable,options};return 1;}
 if(!inline_experiment.enabled)return 1;
 if(options!=inline_experiment.options)return -2;
 std::string error;if(!compiler_hooks->Remove(inliner_token,&error))return 0;inliner_token=nullptr;
 invalidate(RuntimeJitCodeCache());options->debuggable=inline_experiment.compiler_debuggable;
 set_debuggable(RuntimeInstance(),inline_experiment.runtime_debuggable);
 const bool restored=options->debuggable==inline_experiment.compiler_debuggable&&Debuggable()==inline_experiment.runtime_debuggable;
 inline_experiment={};return restored?1:0;
}
int DiagnosticAotExperiment(JNIEnv* env,jobject executable,bool enable){
 NativeObjectAccess access(env);auto* method=Method(access,executable);auto* instrument=RuntimeInstrumentation();const void* code=oat_code(method,sizeof(void*));
 if(enable){auto* options=Options();if(!options||!options->debuggable)return -2;
 if(DiagnosticAllMethodsDeoptimized())return -3;if(!code)return -2;
 const bool previous=Debuggable();
 set_debuggable(RuntimeInstance(),false);initialize(instrument,method,code);
 if(reinterpret_cast<const void*>(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,entry_point)))==code){
  if(!aot_experiment.enabled)aot_experiment={true,previous};
  return 1;
 }
 set_debuggable(RuntimeInstance(),previous);initialize(instrument,method,code);return 0;}
 const bool restored=aot_experiment.enabled?aot_experiment.runtime_debuggable:Debuggable();
 if(Debuggable()!=restored)set_debuggable(RuntimeInstance(),restored);
 initialize(instrument,method,code);aot_experiment={};
 return Debuggable()==restored && (!restored || !code ||
     reinterpret_cast<const void*>(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,entry_point)))!=code)?1:0;
}
int DiagnosticJitCompilerOptionsState(){if(!RuntimeJit()||!*compiler_slot)return -1;auto* options=Options();return (options->debuggable?1:0)|(options->baseline?2:0)|(options->inline_max?4:0)|(BaselineInterface(*compiler_slot)?8:0);}
int DiagnosticPauseJitForGraphHook(bool pause){auto* jit=RuntimeJit();if(!jit||!Read<void*>(jit,offsetof(a::Jit,pool)))return -1;if(pause)stop(jit);else start(jit);return 1;}
int DiagnosticConfigureGraphInlineProbe(JNIEnv* env,jobject caller_ref,jobject callee_ref,void* function){
 NativeObjectAccess access(env);Require(access.Supported());
 if(!function){original.store(nullptr,std::memory_order_release);caller.store(nullptr,std::memory_order_relaxed);callee.store(nullptr,std::memory_order_relaxed);return 1;}
 hits.store(0,std::memory_order_relaxed);caller.store(Method(access,caller_ref),std::memory_order_relaxed);callee.store(Method(access,callee_ref),std::memory_order_relaxed);original.store(reinterpret_cast<Inline>(function),std::memory_order_release);return 1;
}
void* DiagnosticHGraphInlineInto(void* callee_graph,void* outer_graph,void* invoke){auto fn=original.load(std::memory_order_acquire);if(!fn)return nullptr;bool target=Read<void*>(callee_graph,offsetof(a::Graph,method))==callee.load(std::memory_order_relaxed)&&Read<void*>(outer_graph,offsetof(a::Graph,method))==caller.load(std::memory_order_relaxed);auto* result=fn(callee_graph,outer_graph,invoke);if(target)hits.fetch_add(1,std::memory_order_relaxed);return result;}
uint64_t DiagnosticGraphInlineHits(){return hits.load(std::memory_order_relaxed);}
bool ConfigureDeviceCompilerDiagnostics(HookEngine& hooks,const DeviceProfile& profile,std::string* error){
  if(!MatchCompilerDeviceProfile(profile,error))return false;
#define SYMBOL(variable,name) variable=reinterpret_cast<decltype(variable)>(hooks.FindSymbol("libart.so",name));if(!variable){*error="Missing named compiler diagnostic symbol: " name;return false;}
 SYMBOL(suspend_ctor,"_ZN3art3jit16ScopedJitSuspendC2Ev") SYMBOL(suspend_dtor,"_ZN3art3jit16ScopedJitSuspendD2Ev")
 SYMBOL(stop,"_ZN3art3jit3Jit4StopEv") SYMBOL(start,"_ZN3art3jit3Jit5StartEv")
 SYMBOL(set_debuggable,"_ZN3art7Runtime17SetJavaDebuggableEb") SYMBOL(callbacks,"_ZN3art7Runtime19GetRuntimeCallbacksEv")
 SYMBOL(inspected,"_ZN3art16RuntimeCallbacks22IsMethodBeingInspectedEPNS_9ArtMethodE")
 SYMBOL(deoptimized,"_ZN3art15instrumentation15Instrumentation13IsDeoptimizedEPNS_9ArtMethodE")
 SYMBOL(visible,"_ZN3art11ClassLinker40MakeInitializedClassesVisiblyInitializedEPNS_6ThreadEb")
 if(!jit_service::Compiler::Bind(33,[&](const char* name){return hooks.FindSymbol("libart.so",name);},&compile)){
  *error="Missing named compiler diagnostic CompileMethod";return false;
 }
 SYMBOL(initialize,"_ZN3art15instrumentation15Instrumentation21InitializeMethodsCodeEPNS_9ArtMethodEPKv")
 SYMBOL(oat_code,"_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE")
 SYMBOL(compiler_slot,"_ZN3art3jit3Jit13jit_compiler_E")
 SYMBOL(invalidate,"_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv") SYMBOL(root_reader,"artReadBarrierForRootSlow")
#undef SYMBOL
 inliner_target=hooks.FindSymbol("libart-compiler.so","_ZN3art8HInliner3RunEv");
 try_inline=reinterpret_cast<TryInline>(hooks.FindSymbol("libart-compiler.so","_ZN3art8HInliner9TryInlineEPNS_7HInvokeEPb"));
 update_budget=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart-compiler.so","_ZN3art8HInliner20UpdateInliningBudgetEv"));
 if(!inliner_target||!try_inline||!update_budget){*error="Missing named debug-graph inliner functions";return false;}
 compiler_hooks=&hooks;return true;
}
}

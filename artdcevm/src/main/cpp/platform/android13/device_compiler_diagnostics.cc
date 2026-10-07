#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/android13/device_compiler_diagnostics_abi.h"
#include "platform/android13/device_diagnostics.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
namespace artdcevm::android13 {
namespace {
namespace a=compiler_diagnostics_abi;
void (*suspend_ctor)(a::Suspend*),(*suspend_dtor)(a::Suspend*);
void (*stop)(void*),(*start)(void*),(*set_debuggable)(void*,bool);
void* (*callbacks)(void*);
bool (*inspected)(void*,void*),(*deoptimized)(void*,void*);
void (*visible)(void*,void*,bool),(*initialize)(void*,void*,const void*);
bool (*compile)(void*,void*,void*,uint32_t,bool);
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
uint32_t Flags(void* method){return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(art_method_abi::ArtMethod,access_flags)),__ATOMIC_RELAXED);}
// JitCompilerInterface: D1/D0, CompileMethod, TypesLoaded, GenerateDebugInfo,
// ParseCompilerOptions, IsBaselineCompiler. No device STL return is invoked.
bool BaselineInterface(void* compiler){auto** table=*static_cast<void***>(compiler);using Fn=bool (*)(const void*);return reinterpret_cast<Fn>(table[6])(compiler);}
}
int DiagnosticForceOptimizedJit(JNIEnv* env,jobject executable){
 auto* jit=RuntimeJit();if(!jit)return -1;Suspend suspend;NativeObjectAccess access(env);auto* method=Method(access,executable);
 if(Debuggable()||DiagnosticAllMethodsDeoptimized())return -2;
 if(inspected(callbacks(RuntimeInstance()),method))return -3;
 uint32_t flags=Flags(method);
 if(!(flags&a::kIntrinsic)&&!(flags&a::kPreCompiled)&&(flags&a::kDontCompile))return -4;
 if(deoptimized(RuntimeInstrumentation(),method))return -5;
 auto* klass=root_reader(method);
 if(Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,access_flags))&0x40000)return -6;
 visible(RuntimeClassLinker(),access.Self(),true);
 return compile(jit,method,access.Self(),2,false)?1:0;
}
int DiagnosticJitInlineExperiment(JNIEnv* env,bool enable){
 auto* jit=RuntimeJit();if(!jit||!*compiler_slot)return -1;Suspend suspend;NativeObjectAccess access(env);Require(access.Supported());auto* options=Options();
 if(enable){if(!Debuggable()||!options->debuggable||DiagnosticAllMethodsDeoptimized())return -2;
 options->debuggable=false;set_debuggable(RuntimeInstance(),false);return !options->debuggable&&!Debuggable()?1:0;}
 invalidate(RuntimeJitCodeCache());options->debuggable=true;set_debuggable(RuntimeInstance(),true);return options->debuggable&&Debuggable()?1:0;
}
int DiagnosticAotExperiment(JNIEnv* env,jobject executable,bool enable){
 NativeObjectAccess access(env);auto* method=Method(access,executable);auto* instrument=RuntimeInstrumentation();const void* code=oat_code(method,sizeof(void*));
 if(enable){if(DiagnosticAllMethodsDeoptimized())return -3;if(!code)return -2;
 set_debuggable(RuntimeInstance(),false);initialize(instrument,method,code);
 if(reinterpret_cast<const void*>(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,entry_point)))==code)return 1;
 set_debuggable(RuntimeInstance(),true);initialize(instrument,method,code);return 0;}
 set_debuggable(RuntimeInstance(),true);initialize(instrument,method,code);
 return code&&reinterpret_cast<const void*>(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,entry_point)))==code?0:1;
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
 SYMBOL(compile,"_ZN3art3jit3Jit13CompileMethodEPNS_9ArtMethodEPNS_6ThreadENS_15CompilationKindEb")
 SYMBOL(initialize,"_ZN3art15instrumentation15Instrumentation21InitializeMethodsCodeEPNS_9ArtMethodEPKv")
 SYMBOL(oat_code,"_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE")
 SYMBOL(compiler_slot,"_ZN3art3jit3Jit13jit_compiler_E")
 SYMBOL(invalidate,"_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv") SYMBOL(root_reader,"artReadBarrierForRootSlow")
#undef SYMBOL
 return true;
}
}

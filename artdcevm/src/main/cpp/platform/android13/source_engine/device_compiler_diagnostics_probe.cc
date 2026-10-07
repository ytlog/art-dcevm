#include "platform/android13/device_compiler_diagnostics_abi.h"
#include "art_method-inl.h"
#include "runtime.h"
#include "runtime_callbacks.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_compiler.h"
#include "driver/compiler_options.h"
#include "optimizing/nodes.h"
#include "scoped_thread_state_change-inl.h"
#include <cstddef>
namespace art {
struct CompilerDiagnosticsLayoutProbe {
 static void Verify(){namespace a=artdcevm::android13::compiler_diagnostics_abi;
  static_assert(offsetof(jit::Jit,thread_pool_)==offsetof(a::Jit,pool));
  static_assert(offsetof(jit::JitCompiler,compiler_options_)==offsetof(a::Compiler,options));
  static_assert(offsetof(CompilerOptions,inline_max_code_units_)==offsetof(a::Options,inline_max));
  static_assert(offsetof(CompilerOptions,baseline_)==offsetof(a::Options,baseline));
  static_assert(offsetof(CompilerOptions,debuggable_)==offsetof(a::Options,debuggable));
  static_assert(sizeof(HashSet<std::string>)==sizeof(a::HashSet));
  static_assert(sizeof(ArenaVector<void*>)==sizeof(a::ArenaVector));
  static_assert(sizeof(ArenaSafeMap<int32_t,HIntConstant*>)==sizeof(a::ArenaMap));
  static_assert(sizeof(HandleCache)==sizeof(a::Graph::handle_cache));
  static_assert(sizeof(ArenaBitVectorArray)==sizeof(a::BitVectorArray));
  static_assert(offsetof(HGraph,art_method_)==offsetof(a::Graph,method));
  static_assert(sizeof(instrumentation::Instrumentation)==sizeof(a::Instrumentation));
  static_assert(sizeof(Mutex)==sizeof(a::Mutex));static_assert(sizeof(CompatFramework)==sizeof(a::Compat));
  static_assert(offsetof(Runtime,is_java_debuggable_)-offsetof(Runtime,instrumentation_)==offsetof(a::RuntimePolicy,java_debuggable));
  static_assert(sizeof(jit::ScopedJitSuspend)==sizeof(a::Suspend));
  static_assert(static_cast<uint32_t>(CompilationKind::kOptimized)==2);
  static_assert(kAccIntrinsic==a::kIntrinsic&&kAccPreCompiled==a::kPreCompiled&&kAccCompileDontBother==a::kDontCompile);
 }
};
}
extern "C" int ArtDcevmSourceProbeCompilerDiagnostics(JNIEnv* env,int (*read_options)(),bool (*read_debuggable)()){
 art::CompilerDiagnosticsLayoutProbe::Verify();art::ScopedObjectAccess soa(env);
 auto* runtime=art::Runtime::Current();auto* jit=runtime->GetJit();
 int expected=-1;
 if(jit&&jit->GetJitCompiler()){
  auto* compiler=static_cast<art::jit::JitCompiler*>(jit->GetJitCompiler());const auto& options=compiler->GetCompilerOptions();
  expected=(options.GetDebuggable()?1:0)|(options.IsBaseline()?2:0)|(options.GetInlineMaxCodeUnits()?4:0)|(jit->GetJitCompiler()->IsBaselineCompiler()?8:0);
 }
 if(read_options()!=expected)return 1;
 if(read_debuggable()!=runtime->IsJavaDebuggable())return 2;
 return 0;
}

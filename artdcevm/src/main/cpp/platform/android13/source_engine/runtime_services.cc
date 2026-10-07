#include <atomic>
#include <cstdint>
namespace art::mirror { class Object; }
// Exported by the device libart. No Heap or collector object crosses this boundary.
extern "C" art::mirror::Object* artReadBarrierMark(art::mirror::Object* object);
namespace { std::atomic<uint64_t> read_barrier_calls{0}; }
extern "C" art::mirror::Object* ArtDcevmSourceReadBarrierMark(art::mirror::Object* object) {
  read_barrier_calls.fetch_add(1, std::memory_order_relaxed);
  return artReadBarrierMark(object);
}
extern "C" uint64_t ArtDcevmSourceReadBarrierCalls() {
  return read_barrier_calls.load(std::memory_order_relaxed);
}

// Opt-in diagnostics for foreground/deoptimization conformance. Only read source
// declared frame accessors; never modify a PC, register, return value or object.
#include "art_method-inl.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "mirror/executable-inl.h"
#include "runtime.h"
#include "runtime_callbacks.h"
#include "instrumentation.h"
#include "art_field-inl.h"
#include "dex/dex_instruction.h"
#include "dex/dex_instruction-inl.h"
#include "mirror/class-inl.h"
#include "thread.h"
#include "thread_list.h"
#include "stack.h"
#include "managed_stack.h"
#include "jni/jni_internal.h"
#include "scoped_thread_state_change-inl.h"
#include "interpreter/shadow_frame-inl.h"
#include "interpreter/mterp/nterp.h"
#include "oat_quick_method_header.h"
#include "oat_file.h"
#include "stack_map.h"
#include "driver/compiler_options.h"
#include "jit/jit_compiler.h"
#include "optimizing/nodes.h"
#include "jvalue.h"
#include <android/log.h>
#include <cstring>
namespace { std::atomic<bool> trace_deopt{false}; }
namespace {
std::atomic<uint64_t> debuggable_aot_async_deopts{0};
std::atomic<art::Thread*> pending_suspend_deopt_thread{nullptr};
std::atomic<bool> probe_suspend_deopt_enabled{false};
std::atomic<uint64_t> suspend_deopt_hits{0};
}
extern "C" [[noreturn]] void artDeoptimize(art::Thread* self);
extern "C" bool ArtDcevmSourceMarkSuspendDeoptThread(art::Thread* thread) {
  art::Thread* expected = nullptr;
  return pending_suspend_deopt_thread.compare_exchange_strong(
      expected, thread, std::memory_order_acq_rel);
}
extern "C" bool ArtDcevmSourceSuspendDeoptPending(art::Thread* thread) {
  return pending_suspend_deopt_thread.load(std::memory_order_acquire) == thread;
}
extern "C" void ArtDcevmSourceCancelSuspendDeoptThread(art::Thread* thread) {
  pending_suspend_deopt_thread.compare_exchange_strong(
      thread, nullptr, std::memory_order_acq_rel);
}
extern "C" void ArtDcevmSourceFinishPendingSuspendDeopt(art::Thread* self) {
  art::Thread* expected = self;
  if (!pending_suspend_deopt_thread.compare_exchange_strong(
          expected, nullptr, std::memory_order_acq_rel)) return;
  suspend_deopt_hits.fetch_add(1, std::memory_order_relaxed);
  art::JValue result;
  result.SetJ(0);
  self->PushDeoptimizationContext(result, false, nullptr, false,
      art::DeoptimizationMethodType::kDefault);
  artDeoptimize(self);
}
extern "C" int ArtDcevmSourceAotSuspendDeoptProbe(JNIEnv* env, jobject peer, jobject executable) {
  art::Thread* self = art::Thread::Current();
  auto* thread_list = art::Runtime::Current()->GetThreadList();
  bool timed_out = false;
  art::Thread* target = thread_list->SuspendThreadByPeer(
      peer, art::SuspendReason::kInternal, &timed_out);
  if (target == nullptr) return timed_out ? -2 : -1;
  bool aot_frame = false;
  {
    art::ScopedObjectAccess soa(env);
    auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
    const void* code = method->GetOatMethodQuickCode(
        art::Runtime::Current()->GetClassLinker()->GetImagePointerSize());
    const auto* header = code == nullptr ? nullptr
        : art::OatQuickMethodHeader::FromEntryPoint(code);
    art::StackVisitor::WalkStack(
        [&](const art::StackVisitor* frame) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          if (frame->GetMethod() == method && frame->GetCurrentQuickFrame() != nullptr &&
              frame->GetCurrentOatQuickMethodHeader() == header) {
            aot_frame = true;
            return false;
          }
          return true;
        }, target, nullptr, art::StackVisitor::StackWalkKind::kSkipInlinedFrames);
    if (aot_frame) {
      probe_suspend_deopt_enabled.store(true, std::memory_order_release);
      aot_frame = ArtDcevmSourceMarkSuspendDeoptThread(target);
    }
  }
  bool resumed = thread_list->Resume(target, art::SuspendReason::kInternal);
  return !resumed ? -3 : aot_frame ? 1 : 0;
}
extern "C" uint64_t ArtDcevmSourceAotSuspendDeoptHits(bool reset) {
  if (reset) {
    pending_suspend_deopt_thread.store(nullptr, std::memory_order_release);
    probe_suspend_deopt_enabled.store(false, std::memory_order_release);
  }
  return suspend_deopt_hits.load(std::memory_order_acquire);
}
extern "C" uint64_t ArtDcevmSourceDebuggableAotAsyncDeopts() {
  return debuggable_aot_async_deopts.load(std::memory_order_relaxed);
}
extern "C" bool ArtDcevmSourceReceiverGuardRequired();
extern "C" bool ArtDcevmSourceAllowDebuggableAotDeopt(
    const art::Runtime* runtime, uintptr_t pc) {
  if (!ArtDcevmSourceReceiverGuardRequired() &&
      !probe_suspend_deopt_enabled.load(std::memory_order_acquire)) return false;
  art::Thread* self = art::Thread::Current();
  if (self == nullptr) return false;
  bool safe_aot_frame = false;
  art::StackVisitor::WalkStack(
      [&](const art::StackVisitor* frame) REQUIRES_SHARED(art::Locks::mutator_lock_) {
        if (frame->GetCurrentQuickFrame() == nullptr ||
            frame->GetCurrentQuickFramePc() != pc) return true;
        art::ArtMethod* method = frame->GetMethod();
        if (method == nullptr || method->IsRuntimeMethod() || method->IsNative() ||
            method->GetDeclaringClass()->GetClassLoader() == nullptr) return false;
        const art::DexFile* dex = method->GetDexFile();
        const art::OatDexFile* oat_dex = dex == nullptr ? nullptr : dex->GetOatDexFile();
        const art::OatFile* oat = oat_dex == nullptr ? nullptr : oat_dex->GetOatFile();
        if (oat == nullptr || !oat->IsDebuggable()) return false;
        const void* code = method->GetOatMethodQuickCode(
            runtime->GetClassLinker()->GetImagePointerSize());
        if (code == nullptr) return false;
        const auto* header = art::OatQuickMethodHeader::FromEntryPoint(code);
        // The pinned device's AOT header uses a vendor layout. Compare the
        // runtime-provided header identity; do not decode its size flags here.
        safe_aot_frame = frame->GetCurrentOatQuickMethodHeader() == header &&
            pc >= reinterpret_cast<uintptr_t>(code);
        return false;
      }, self, nullptr, art::StackVisitor::StackWalkKind::kSkipInlinedFrames);
  if (safe_aot_frame) debuggable_aot_async_deopts.fetch_add(1, std::memory_order_relaxed);
  return safe_aot_frame;
}
// 0: no JIT, 1: JIT available but this Method has no code, 2: code cached.
// This reads ART's source-declared JIT API; it does not derive an address.
extern "C" int ArtDcevmSourceJitMethodState(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr) return 0;
  return jit->GetCodeCache()->ContainsMethod(method) ? 2 : 1;
}
// Bit 0: an AOT method body exists. Bit 1: the method currently enters that
// body. Bit 2: the JIT cache owns compiled code. Bits 3-6 classify a bridge
// and whether instrumentation would dispatch to AOT. All pointers come from ART's
// source-declared accessors; no binary-derived address is used.
extern "C" int ArtDcevmSourceAotMethodState(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* runtime = art::Runtime::Current();
  const void* oat_code = method->GetOatMethodQuickCode(
      runtime->GetClassLinker()->GetImagePointerSize());
  const void* entry = method->GetEntryPointFromQuickCompiledCode();
  auto* jit = runtime->GetJit();
  auto* instrumentation = runtime->GetInstrumentation();
  return (oat_code != nullptr ? 1 : 0)
      | (oat_code != nullptr && entry == oat_code ? 2 : 0)
      | (jit != nullptr && jit->GetCodeCache()->ContainsMethod(method) ? 4 : 0)
      | (runtime->GetClassLinker()->IsQuickToInterpreterBridge(entry) ? 8 : 0)
      | (entry == art::interpreter::GetNterpEntryPoint() ? 16 : 0)
      | (instrumentation->AreAllMethodsDeoptimized() ? 64 : 0)
      | (oat_code != nullptr && instrumentation->GetCodeForInvoke(method) == oat_code ? 128 : 0);
}
// Isolated test-process experiment. Restore the debuggable policy after the
// caller has finished; never leave a production transaction in this state.
extern "C" int ArtDcevmSourceAotExperiment(JNIEnv* env, jobject executable, bool enable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* runtime = art::Runtime::Current();
  auto* instrumentation = runtime->GetInstrumentation();
  const void* oat_code = method->GetOatMethodQuickCode(
      runtime->GetClassLinker()->GetImagePointerSize());
  if (enable) {
    if (instrumentation->AreAllMethodsDeoptimized()) return -3;
    if (oat_code == nullptr) return -2;
    // The isolated mixed-call-chain probe enables a second AOT method while
    // its first method has already switched the process policy.
    runtime->SetJavaDebuggable(false);
    instrumentation->InitializeMethodsCode(method, oat_code);
    if (method->GetEntryPointFromQuickCompiledCode() == oat_code) return 1;
    runtime->SetJavaDebuggable(true);
    instrumentation->InitializeMethodsCode(method, oat_code);
    return 0;
  }
  runtime->SetJavaDebuggable(true);
  instrumentation->InitializeMethodsCode(method, oat_code);
  return oat_code != nullptr && method->GetEntryPointFromQuickCompiledCode() == oat_code ? 0 : 1;
}
// Bit 0: the target frame exists. Bit 1: it is a quick frame. Bit 2: the
// suspended frame's source-reported PC lies inside the method's AOT code.
extern "C" int ArtDcevmSourceAotFrameState(JNIEnv* env, jobject peer, jobject executable) {
  auto* thread_list = art::Runtime::Current()->GetThreadList();
  bool timed_out = false;
  art::Thread* target = thread_list->SuspendThreadByPeer(
      peer, art::SuspendReason::kInternal, &timed_out);
  if (target == nullptr) return timed_out ? -2 : -1;
  int state = 0;
  {
    art::ScopedObjectAccess soa(env);
    auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
    const void* oat_code = method->GetOatMethodQuickCode(
        art::Runtime::Current()->GetClassLinker()->GetImagePointerSize());
    const art::OatQuickMethodHeader* header = oat_code == nullptr ? nullptr
        : art::OatQuickMethodHeader::FromEntryPoint(oat_code);
    art::StackVisitor::WalkStack(
        [&](const art::StackVisitor* frame) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          if (frame->GetMethod() != method) return true;
          state |= 1;
          if (frame->GetCurrentQuickFrame() != nullptr) state |= 2;
          if (header != nullptr && frame->GetCurrentQuickFramePc() != 0 &&
              header->Contains(frame->GetCurrentQuickFramePc())) state |= 4;
          return false;
        }, target, nullptr, art::StackVisitor::StackWalkKind::kSkipInlinedFrames);
  }
  bool resumed = thread_list->Resume(target, art::SuspendReason::kInternal);
  return resumed ? state : -3;
}
// Bit 0: caller frame found. Bit 1: its quick PC is inside active JIT code.
// Bit 2: logical callee frame found when ART includes inlined frames.
// Bit 3: that callee frame shares the caller's JIT code range.
extern "C" int ArtDcevmSourceJitActiveFrameState(
    JNIEnv* env, jobject peer, jobject caller_executable, jobject callee_executable) {
  auto* runtime = art::Runtime::Current();
  auto* thread_list = runtime->GetThreadList();
  bool timed_out = false;
  art::Thread* target = thread_list->SuspendThreadByPeer(
      peer, art::SuspendReason::kInternal, &timed_out);
  if (target == nullptr) return timed_out ? -2 : -1;
  int state = 0;
  {
    art::ScopedObjectAccess soa(env);
    auto* caller = soa.Decode<art::mirror::Executable>(caller_executable)->GetArtMethod();
    auto* callee = soa.Decode<art::mirror::Executable>(callee_executable)->GetArtMethod();
    const void* entry = caller->GetEntryPointFromQuickCompiledCode();
    auto* jit = runtime->GetJit();
    const art::OatQuickMethodHeader* header =
        jit != nullptr && jit->GetCodeCache()->ContainsPc(entry)
            ? art::OatQuickMethodHeader::FromEntryPoint(entry) : nullptr;
    art::StackVisitor::WalkStack(
        [&](const art::StackVisitor* frame) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          art::ArtMethod* method = frame->GetMethod();
          if (method != caller && method != callee) return true;
          bool in_caller_code = header != nullptr && frame->GetCurrentQuickFrame() != nullptr &&
              frame->GetCurrentQuickFramePc() != 0 &&
              header->Contains(frame->GetCurrentQuickFramePc());
          if (method == caller) state |= 1 | (in_caller_code ? 2 : 0);
          if (method == callee) state |= 4 | (in_caller_code ? 8 : 0);
          return true;
        }, target, nullptr, art::StackVisitor::StackWalkKind::kIncludeInlinedFrames);
  }
  bool resumed = thread_list->Resume(target, art::SuspendReason::kInternal);
  return resumed ? state : -3;
}
// 0: no JIT code, 1: cached but not active, 2: active optimized code without
// inline metadata, 3: active optimized code with inline metadata.
extern "C" int ArtDcevmSourceJitInlineState(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr || !jit->GetCodeCache()->ContainsMethod(method)) return 0;
  const void* entry = method->GetEntryPointFromQuickCompiledCode();
  if (!jit->GetCodeCache()->ContainsPc(entry)) return 1;
  const auto* header = art::OatQuickMethodHeader::FromEntryPoint(entry);
  if (!header->IsOptimized()) return 2;
  return art::CodeInfo::HasInlineInfo(header->GetOptimizedCodeInfoPtr()) ? 3 : 2;
}
// 0: no active JIT code, 1: baseline JIT, 2: optimized without inline table,
// 3: optimized with inline table. This separates the two former state-2 cases.
extern "C" int ArtDcevmSourceJitCodeKind(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr) return 0;
  const void* entry = method->GetEntryPointFromQuickCompiledCode();
  if (!jit->GetCodeCache()->ContainsPc(entry)) return 0;
  const auto* header = art::OatQuickMethodHeader::FromEntryPoint(entry);
  if (!header->IsOptimized()) return 1;
  return art::CodeInfo::HasInlineInfo(header->GetOptimizedCodeInfoPtr()) ? 3 : 2;
}
// Bit 0: cached target code exists. Bit 1: cached code has CodeInfo.
// Bit 2: that CodeInfo says baseline. Bit 3: inline table exists.
// Bit 4: cached code is the active method entry.
extern "C" int ArtDcevmSourceJitCacheDetail(JNIEnv* env, jobject executable) {
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr) return -1;
  auto* self = art::Thread::Current();
  const void* entry = method->GetEntryPointFromQuickCompiledCode();
  int state = 0;
  art::MutexLock lock(self, *art::Locks::jit_lock_);
  jit->GetCodeCache()->ArtDcevmVisitAllMethods([&](const void* code, art::ArtMethod* owner) {
    if (owner != method) return;
    state |= 1;
    const auto* header = art::OatQuickMethodHeader::FromCodePointer(code);
    if (header->IsOptimized()) {
      state |= 2;
      const uint8_t* info = header->GetOptimizedCodeInfoPtr();
      if (art::CodeInfo::IsBaseline(info)) state |= 4;
      if (art::CodeInfo::HasInlineInfo(info)) state |= 8;
    }
    if (code == entry) state |= 16;
  });
  return state;
}
extern "C" int ArtDcevmSourceInvalidateAllJit(JNIEnv* env) {
  art::ScopedObjectAccess soa(env);
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr) return -1;
  jit->GetCodeCache()->InvalidateAllCompiledCode();
  return 1;
}
// Transitional ABI accessor for the enhancement library. The enhancement
// library resolves and calls JitCodeCache::InvalidateAllCompiledCode by name.
extern "C" void* ArtDcevmSourceGetJitCodeCache() {
  auto* jit = art::Runtime::Current()->GetJit();
  return jit == nullptr ? nullptr : jit->GetCodeCache();
}
extern "C" int ArtDcevmSourceForceOptimizedJit(JNIEnv* env, jobject executable) {
  auto* runtime = art::Runtime::Current();
  auto* jit = runtime->GetJit();
  if (jit == nullptr) return -1;
  art::jit::ScopedJitSuspend suspend;
  art::ScopedObjectAccess soa(env);
  auto* method = soa.Decode<art::mirror::Executable>(executable)->GetArtMethod();
  if (runtime->IsJavaDebuggable() ||
      runtime->GetInstrumentation()->AreAllMethodsDeoptimized()) return -2;
  if (runtime->GetRuntimeCallbacks()->IsMethodBeingInspected(method)) return -3;
  if (!method->IsCompilable()) return -4;
  if (runtime->GetInstrumentation()->IsDeoptimized(method)) return -5;
  if (method->IsProxyMethod()) return -6;
  auto* self = art::Thread::Current();
  runtime->GetClassLinker()->MakeInitializedClassesVisiblyInitialized(self, /*wait=*/true);
  return jit->CompileMethod(method, self, art::CompilationKind::kOptimized,
                            /*prejit=*/false) ? 1 : 0;
}
// Isolated test only. The caller checks the exact device compiler Build-ID.
// Quiesce compilation before changing the source-declared compiler option;
// its OptimizingCompiler holds a reference to the same CompilerOptions object.
extern "C" int ArtDcevmSourceJitInlineExperiment(JNIEnv* env, bool enable) {
  auto* runtime = art::Runtime::Current();
  auto* jit = runtime->GetJit();
  if (jit == nullptr || jit->GetJitCompiler() == nullptr) return -1;
  art::jit::ScopedJitSuspend suspend;
  art::ScopedObjectAccess soa(env);
  auto* compiler = static_cast<art::jit::JitCompiler*>(jit->GetJitCompiler());
  auto& options = const_cast<art::CompilerOptions&>(compiler->GetCompilerOptions());
  if (enable) {
    if (!runtime->IsJavaDebuggable() || !options.GetDebuggable() ||
        runtime->GetInstrumentation()->AreAllMethodsDeoptimized()) return -2;
    options.SetDebuggable(false);
    runtime->SetJavaDebuggable(false);
    return !options.GetDebuggable() && !runtime->IsJavaDebuggable() ? 1 : 0;
  }
  // Any code built during the experiment must stop receiving new calls before
  // the process returns to its normal debuggable compilation policy.
  jit->GetCodeCache()->InvalidateAllCompiledCode();
  options.SetDebuggable(true);
  runtime->SetJavaDebuggable(true);
  return options.GetDebuggable() && runtime->IsJavaDebuggable() ? 1 : 0;
}
// Bit 0: compiler debug mode. Bit 1: concrete baseline mode. Bit 2: inlining
// budget enabled. Bit 3: JIT interface reports baseline-only mode.
extern "C" int ArtDcevmSourceJitCompilerOptionsState() {
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr || jit->GetJitCompiler() == nullptr) return -1;
  auto* compiler = static_cast<art::jit::JitCompiler*>(jit->GetJitCompiler());
  const auto& options = compiler->GetCompilerOptions();
  return (options.GetDebuggable() ? 1 : 0) | (options.IsBaseline() ? 2 : 0)
      | (options.GetInlineMaxCodeUnits() != 0 ? 4 : 0)
      | (jit->GetJitCompiler()->IsBaselineCompiler() ? 8 : 0);
}
namespace {
using GraphInline = art::HInstruction* (*)(art::HGraph*, art::HGraph*, art::HInvoke*);
std::atomic<GraphInline> graph_inline_original{nullptr};
std::atomic<art::ArtMethod*> graph_inline_caller{nullptr};
std::atomic<art::ArtMethod*> graph_inline_callee{nullptr};
std::atomic<uint64_t> graph_inline_hits{0};
}
// The loader installs this at the named, Build-ID-pinned HGraph::InlineInto
// symbol. Read source-declared graph ownership before the original mutates it.
extern "C" art::HInstruction* ArtDcevmSourceHGraphInlineInto(
    art::HGraph* callee_graph, art::HGraph* outer_graph, art::HInvoke* invoke) {
  GraphInline original = graph_inline_original.load(std::memory_order_acquire);
  if (original == nullptr) return nullptr;
  bool target = callee_graph->GetArtMethod() == graph_inline_callee.load(std::memory_order_relaxed)
      && outer_graph->GetArtMethod() == graph_inline_caller.load(std::memory_order_relaxed);
  art::HInstruction* replacement = original(callee_graph, outer_graph, invoke);
  if (target) graph_inline_hits.fetch_add(1, std::memory_order_relaxed);
  return replacement;
}
extern "C" int ArtDcevmSourceConfigureGraphInlineProbe(
    JNIEnv* env, jobject caller, jobject callee, void* original) {
  art::ScopedObjectAccess soa(env);
  if (original == nullptr) {
    graph_inline_original.store(nullptr, std::memory_order_release);
    graph_inline_caller.store(nullptr, std::memory_order_relaxed);
    graph_inline_callee.store(nullptr, std::memory_order_relaxed);
    return 1;
  }
  graph_inline_hits.store(0, std::memory_order_relaxed);
  graph_inline_caller.store(
      soa.Decode<art::mirror::Executable>(caller)->GetArtMethod(), std::memory_order_relaxed);
  graph_inline_callee.store(
      soa.Decode<art::mirror::Executable>(callee)->GetArtMethod(), std::memory_order_relaxed);
  graph_inline_original.store(reinterpret_cast<GraphInline>(original), std::memory_order_release);
  return 1;
}
extern "C" uint64_t ArtDcevmSourceGraphInlineHits() {
  return graph_inline_hits.load(std::memory_order_relaxed);
}
extern "C" int ArtDcevmSourcePauseJitForGraphHook(bool pause) {
  auto* jit = art::Runtime::Current()->GetJit();
  if (jit == nullptr || jit->GetThreadPool() == nullptr) return -1;
  if (pause) jit->Stop(); else jit->Start();
  return 1;
}
extern "C" bool ArtDcevmSourceAllMethodsDeoptimized() {
  return art::Runtime::Current()->GetInstrumentation()->AreAllMethodsDeoptimized();
}
extern "C" bool ArtDcevmSourceReceiverGuardEnabled();
namespace {
std::atomic<uint64_t> field_probe_hits{0};
std::atomic<uint64_t> invalid_receiver_hits{0};
std::atomic<uint64_t> invoke_receiver_checks{0};
std::atomic<uint64_t> invoke_receiver_rejections{0};
std::atomic<uint64_t> invoke_frame_misses{0};
std::atomic<bool> field_probe_enabled{false};
std::atomic<bool> receiver_guard_required{false};
bool field_probe_force_reject = false;
void GuardFieldReceiver(art::Thread* thread, art::Handle<art::mirror::Object> receiver,
                        art::ArtField* field) {
  if (receiver.IsNull()) return;
  if (!field->GetDeclaringClass()->IsAssignableFrom(receiver->GetClass())) {
    invalid_receiver_hits.fetch_add(1, std::memory_order_relaxed);
    thread->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                              "Receiver is not an instance of the field declaring class");
    return;
  }
  if (!field_probe_force_reject || std::strcmp(field->GetName(), "ancestor") != 0 ||
      !field->GetDeclaringClass()->DescriptorEquals("Lcom/github/ytlog/artdcevm/fixture/NarrowParent;")) return;
  field_probe_hits.fetch_add(1, std::memory_order_relaxed);
  thread->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                            "Field access probe rejected access before storage mutation");
}
class FieldReadProbe final : public art::instrumentation::InstrumentationListener {
 public:
  void MethodEntered(art::Thread*, art::ArtMethod*) override {}
  void MethodExited(art::Thread*, art::ArtMethod*, art::instrumentation::OptionalFrame,
                    art::JValue&) override {}
  void MethodUnwind(art::Thread*, art::Handle<art::mirror::Object>, art::ArtMethod*,
                    uint32_t) override {}
  void DexPcMoved(art::Thread* thread, art::Handle<art::mirror::Object>,
                  art::ArtMethod* method, uint32_t dex_pc) override {
    if (method->GetCodeItem() == nullptr) return;
    art::CodeItemDataAccessor accessor(method->DexInstructionData());
    if (dex_pc >= accessor.InsnsSizeInCodeUnits()) return;
    const art::Instruction* inst = accessor.Insns() == nullptr ? nullptr
        : art::Instruction::At(accessor.Insns() + dex_pc);
    if (inst == nullptr) return;
    bool range;
    switch (inst->Opcode()) {
      case art::Instruction::INVOKE_VIRTUAL:
      case art::Instruction::INVOKE_SUPER:
      case art::Instruction::INVOKE_DIRECT:
      case art::Instruction::INVOKE_INTERFACE:
        range = false;
        break;
      case art::Instruction::INVOKE_VIRTUAL_RANGE:
      case art::Instruction::INVOKE_SUPER_RANGE:
      case art::Instruction::INVOKE_DIRECT_RANGE:
      case art::Instruction::INVOKE_INTERFACE_RANGE:
        range = true;
        break;
      default:
        return;
    }
    art::ShadowFrame* frame = thread->GetManagedStack()->GetTopShadowFrame();
    if (frame == nullptr || frame->GetMethod() != method || frame->GetDexPC() != dex_pc) {
      invoke_frame_misses.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    uint32_t receiver_reg = range ? inst->VRegC_3rc() : inst->VRegC_35c();
    if (receiver_reg >= frame->NumberOfVRegs()) return;
    if (frame->GetVRegReference(receiver_reg) == nullptr) return;
    uint32_t method_index = range ? inst->VRegB_3rc() : inst->VRegB_35c();
    const art::DexFile* dex = method->GetDexFile();
    if (method_index >= dex->NumMethodIds()) return;
    const art::dex::MethodId& method_id = dex->GetMethodId(method_index);
    art::ObjPtr<art::mirror::Class> declared = method->ResolveClassFromTypeIndex(method_id.class_idx_);
    if (declared == nullptr) return;
    invoke_receiver_checks.fetch_add(1, std::memory_order_relaxed);
    art::ObjPtr<art::mirror::Object> receiver(frame->GetVRegReference(receiver_reg));
    if (!declared->IsAssignableFrom(receiver->GetClass())) {
      invoke_receiver_rejections.fetch_add(1, std::memory_order_relaxed);
      thread->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                                "Receiver is not an instance of the invoked method declaring class");
    }
  }
  void FieldRead(art::Thread* thread, art::Handle<art::mirror::Object> receiver,
                 art::ArtMethod*, uint32_t, art::ArtField* field) override {
    GuardFieldReceiver(thread, receiver, field);
  }
  void FieldWritten(art::Thread* thread, art::Handle<art::mirror::Object> receiver,
                    art::ArtMethod*, uint32_t, art::ArtField* field,
                    const art::JValue&) override {
    GuardFieldReceiver(thread, receiver, field);
  }
  void FieldWritten(art::Thread* thread, art::Handle<art::mirror::Object> receiver,
                    art::ArtMethod*, uint32_t, art::ArtField* field,
                    art::Handle<art::mirror::Object>) override {
    GuardFieldReceiver(thread, receiver, field);
  }
  void ExceptionThrown(art::Thread*, art::Handle<art::mirror::Throwable>) override {}
  void ExceptionHandled(art::Thread*, art::Handle<art::mirror::Throwable>) override {}
  void Branch(art::Thread*, art::ArtMethod*, uint32_t, int32_t) override {}
  void WatchedFramePop(art::Thread*, const art::ShadowFrame&) override {}
};
FieldReadProbe field_read_probe;
}
extern "C" bool ArtDcevmSourceReceiverGuardEnabled() {
  return field_probe_enabled.load(std::memory_order_acquire);
}
extern "C" bool ArtDcevmSourceReceiverGuardRequired() {
  return receiver_guard_required.load(std::memory_order_acquire);
}
extern "C" void ArtDcevmSourceRequireReceiverGuard(bool required) {
  receiver_guard_required.store(required, std::memory_order_release);
}
extern "C" bool ArtDcevmSourceFieldReadProbeMode(bool enable, bool force_reject) {
  auto* self = art::Thread::Current();
  art::ScopedThreadStateChange stsc(self, art::ThreadState::kNative);
  art::ScopedSuspendAll ssa("ART DCEVM field read probe");
  auto* instrumentation = art::Runtime::Current()->GetInstrumentation();
  if (!enable && receiver_guard_required.load(std::memory_order_acquire)) return false;
  if (enable == field_probe_enabled.load(std::memory_order_acquire)) return true;
  if (enable) {
    field_probe_hits.store(0, std::memory_order_relaxed);
    invalid_receiver_hits.store(0, std::memory_order_relaxed);
    invoke_receiver_checks.store(0, std::memory_order_relaxed);
    invoke_receiver_rejections.store(0, std::memory_order_relaxed);
    invoke_frame_misses.store(0, std::memory_order_relaxed);
    field_probe_force_reject = force_reject;
    instrumentation->AddListener(&field_read_probe,
        art::instrumentation::Instrumentation::kFieldRead |
        art::instrumentation::Instrumentation::kFieldWritten |
        art::instrumentation::Instrumentation::kDexPcMoved);
    instrumentation->DeoptimizeEverything("artdcevm-field-read-probe");
  } else {
    instrumentation->RemoveListener(&field_read_probe,
        art::instrumentation::Instrumentation::kFieldRead |
        art::instrumentation::Instrumentation::kFieldWritten |
        art::instrumentation::Instrumentation::kDexPcMoved);
    instrumentation->UndeoptimizeEverything("artdcevm-field-read-probe");
    field_probe_force_reject = false;
  }
  field_probe_enabled.store(enable, std::memory_order_release);
  return true;
}
namespace {
bool GuardJniFieldReceiver(const art::ScopedObjectAccessAlreadyRunnable& soa,
                           jobject receiver, art::ArtField* field) {
  if (!field_probe_enabled.load(std::memory_order_acquire) || receiver == nullptr ||
      field == nullptr || field->IsStatic()) return false;
  art::ObjPtr<art::mirror::Object> object(soa.Decode<art::mirror::Object>(receiver));
  if (object == nullptr || field->GetDeclaringClass()->IsAssignableFrom(object->GetClass()))
    return false;
  soa.Self()->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                                 "JNI field receiver has lost its declaring class");
  return true;
}
bool GuardJniMethodReceiver(const art::ScopedObjectAccessAlreadyRunnable& soa,
                            jobject receiver, art::ArtMethod* method) {
  if (!field_probe_enabled.load(std::memory_order_acquire) || receiver == nullptr ||
      method == nullptr || method->IsStatic()) return false;
  art::ObjPtr<art::mirror::Object> object(soa.Decode<art::mirror::Object>(receiver));
  if (object == nullptr || method->GetDeclaringClass()->IsAssignableFrom(object->GetClass()))
    return false;
  soa.Self()->ThrowNewException("Ljava/lang/IncompatibleClassChangeError;",
                                 "JNI method receiver has lost its declaring class");
  return true;
}
}
extern "C" bool ArtDcevmGuardJniFieldReceiver(JNIEnv* env, jobject receiver, jfieldID id) {
  art::ScopedObjectAccess soa(env);
  return GuardJniFieldReceiver(soa, receiver, art::jni::DecodeArtField(id));
}
extern "C" bool ArtDcevmGuardRunnableFieldReceiver(const void* access, jobject receiver,
                                                     jfieldID id) {
  return GuardJniFieldReceiver(
      *static_cast<const art::ScopedObjectAccessAlreadyRunnable*>(access), receiver,
      art::jni::DecodeArtField(id));
}
extern "C" bool ArtDcevmGuardRunnableMethodReceiver(const void* access, jobject receiver,
                                                      void* method) {
  return GuardJniMethodReceiver(
      *static_cast<const art::ScopedObjectAccessAlreadyRunnable*>(access), receiver,
      static_cast<art::ArtMethod*>(method));
}
extern "C" bool ArtDcevmGuardRunnableMethodIdReceiver(const void* access, jobject receiver,
                                                        jmethodID id) {
  return GuardJniMethodReceiver(
      *static_cast<const art::ScopedObjectAccessAlreadyRunnable*>(access), receiver,
      art::jni::DecodeArtMethod(id));
}
extern "C" uint64_t ArtDcevmSourceFieldReadProbeHits() {
  return field_probe_hits.load(std::memory_order_relaxed);
}
extern "C" uint64_t ArtDcevmSourceInvokeGuardDiagnostics(int index) {
  switch (index) {
    case 0: return invoke_receiver_checks.load(std::memory_order_relaxed);
    case 1: return invoke_receiver_rejections.load(std::memory_order_relaxed);
    case 2: return invoke_frame_misses.load(std::memory_order_relaxed);
    default: return 0;
  }
}
extern "C" void ArtDcevmSourceSetDeoptTracing(bool enabled) { trace_deopt.store(enabled); }
extern "C" void ArtDcevmSourceTraceDeopt(void*, void* frames, void* result, bool from_code, int type) {
  if (!trace_deopt.load(std::memory_order_relaxed)) return;
  auto* frame = static_cast<art::ShadowFrame*>(frames);
  auto* value = static_cast<art::JValue*>(result);
  for (int depth = 0; frame && depth < 64; ++depth, frame = frame->GetLink()) {
    auto* method = frame->GetMethod();
    if (method->GetNameView() != "doFrame") continue;
    uint32_t pc = frame->GetDexPC();
    uint32_t regs = frame->NumberOfVRegs();
    __android_log_print(ANDROID_LOG_INFO, "ArtDcevmDeopt",
        "doFrame depth=%d pc=%u regs=%u from_code=%d type=%d result=0x%llx v2=0x%x ref2=%p",
        depth, pc, regs, from_code, type, static_cast<unsigned long long>(value->GetJ()),
        regs > 2 ? frame->GetVReg(2) : 0,
        regs > 2 ? frame->GetVRegReference(2) : nullptr);
  }
}

#include <vector>
#include "platform/common/narrowed_references.h"
#include "platform/common/fault_injection.h"
#include "platform/symbols/art_symbol_compat.h"
#include <array>
#include <android/log.h>
#include <jni.h>
#include <jvmti.h>
#include <cstdint>

namespace artdcevm::fault_injection {
namespace {
bool interface_prepared = false;
int imt_fallback_mode = 0;
int private_self_mask = 0;
thread_local int failure_stage = 0, failure_hits = 0;
thread_local std::array<int, 5> counters{};
thread_local bool reject_next_probe = false;
thread_local uint64_t reads = 0, marking_reads = 0;
}
void SnapshotFinished(std::chrono::steady_clock::time_point start, int targets, int loaded) {
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
  __android_log_print(ANDROID_LOG_INFO, "ART-DCEVM-SNAPSHOT", "targets=%d loaded=%d nanos=%lld",
                      targets, loaded, static_cast<long long>(nanos));
}
void InterfacePrepared(bool prepared) { interface_prepared = prepared; }
int ImtFallbackMode() { return imt_fallback_mode; }
int PrivateSelfMask() { return private_self_mask; }
bool Inject(int stage) {
  if (failure_stage != stage) return false;
  failure_stage = 0;
  ++failure_hits;
  return true;
}
bool RejectThreadProbe() {
  bool rejected = reject_next_probe;
  reject_next_probe = false;
  return rejected;
}
void CorruptHolder(const abi::arm64::RedefinitionDataIter& data) {
  auto* holder = data.holder->array.Get<abi::arm64::ObjectArray>();
  holder->elements[data.index * abi::arm64::RedefinitionDataIter::kNumSlots +
                   abi::arm64::RedefinitionDataIter::kSlotNewClasses] = 0;
}
void ReferenceRead(bool marking) { ++reads; if (marking) ++marking_reads; }
int Increment(Counter counter) { return ++counters[static_cast<int>(counter)]; }
void Set(Counter counter, int value) { counters[static_cast<int>(counter)] = value; }
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_configureImtFallback(
    JNIEnv*, jclass, jint mode) {
  if (mode < 0 || mode > 1) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  imt_fallback_mode = mode;
  return JVMTI_ERROR_NONE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_configurePrivateSelfMask(
    JNIEnv*, jclass, jint mask) {
  if (mask < 0 || mask > 3) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  private_self_mask = mask;
  return JVMTI_ERROR_NONE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_configureCheckJni(
    JNIEnv* jni, jclass, jboolean enabled) {
  // Test-only process configuration, before InstallJniGuards captures a table.
  // No private runtime layout or unchecked object access is needed here.
  using namespace symbols;
  constexpr NamedSymbol toggle{Library::kArt, "_ZN3art9JavaVMExt18SetCheckJniEnabledEb", SymbolTable::kAnyNamed};
  constexpr NamedSymbol versions[] = {
      {Library::kArt, "_ZN3art3JNIILb0EE10GetVersionEP7_JNIEnv", SymbolTable::kAnyNamed},
      {Library::kArt, "_ZN3art3JNIILb1EE10GetVersionEP7_JNIEnv", SymbolTable::kAnyNamed}};
  auto set = reinterpret_cast<void (*)(JavaVM*, bool)>(Find(toggle));
  void* plain[] = {Find(versions[0]), Find(versions[1])};
  JavaVM* vm = nullptr;
  if (!set || !plain[0] || !plain[1] || jni->GetJavaVM(&vm) != JNI_OK)
    return JVMTI_ERROR_NOT_AVAILABLE;
  set(vm, enabled);
  void* version = reinterpret_cast<void*>(jni->functions->GetVersion);
  const bool checked = version != plain[0] && version != plain[1];
  return checked == (enabled != 0) ? JVMTI_ERROR_NONE : JVMTI_ERROR_INTERNAL;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_suspendJit(JNIEnv*, jclass) {
  // Test-only process configuration for interpreter measurements. The fixed
  // jit.h declaration holds one bool; this suspension is never ended, so the
  // JIT workers stay stopped for the rest of the process.
  using namespace symbols;
  constexpr NamedSymbol suspend{Library::kArt, "_ZN3art3jit16ScopedJitSuspendC1Ev", SymbolTable::kAnyNamed};
  alignas(16) static unsigned char scope[16];
  auto construct = reinterpret_cast<void (*)(void*)>(Find(suspend));
  if (!construct) return JVMTI_ERROR_NOT_AVAILABLE;
  construct(scope);
  return scope[0] ? JVMTI_ERROR_NONE : JVMTI_ERROR_NOT_AVAILABLE;  // was_on_: a JIT was running.
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_primePointerId(
    JNIEnv* jni, jclass, jclass klass, jint kind) {
  // Called before agent initialization. ART may create its own marker when
  // still in swapable-pointer mode; no private heap field is written here.
  uintptr_t id = kind == 0 ? reinterpret_cast<uintptr_t>(jni->GetMethodID(klass, "pointerValue", "()I"))
      : kind == 1 ? reinterpret_cast<uintptr_t>(jni->GetFieldID(klass, "marker", "I"))
                  : reinterpret_cast<uintptr_t>(jni->GetStaticFieldID(klass, "staticMarker", "I"));
  if (jni->ExceptionCheck()) { jni->ExceptionClear(); return -1; }
  return id ? static_cast<jint>((id & 1) == 0) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_armFailure(JNIEnv*, jclass, jint stage) {
  failure_stage = stage;
  failure_hits = 0;
  counters[static_cast<int>(Counter::kDiscardedClasses)] = 0;
  return interface_prepared ? 0 : JVMTI_ERROR_NOT_AVAILABLE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_discardedClasses(JNIEnv*, jclass) {
  return counters[static_cast<int>(Counter::kDiscardedClasses)];
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_allocatedTypes(JNIEnv*, jclass) {
  return counters[static_cast<int>(Counter::kAllocatedTypes)];
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_allocationOrder(JNIEnv*, jclass) {
  return counters[static_cast<int>(Counter::kAllocationOrder)];
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_failureHits(JNIEnv*, jclass) {
  return failure_hits;
}
extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_rejectRuntimeThreadProbe(JNIEnv*, jclass) {
  reject_next_probe = true;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_runtimeReferenceReads(JNIEnv*, jclass) {
  return static_cast<jlong>(reads);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_runtimeMarkingReads(JNIEnv*, jclass) {
  return static_cast<jlong>(marking_reads);
}
}  // namespace artdcevm::fault_injection

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_referenceCleanupVisits(JNIEnv*, jclass) {
  return artdcevm::narrowed_references::Visits();
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_referenceCleanupClears(JNIEnv*, jclass) {
  return artdcevm::narrowed_references::Clears();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_JniReceiverLossInstrumentation_promoteIds(JNIEnv* jni, jclass) {
  // Test1972 and runtime.h, fixed Android 13/16: SetJniIdType only permits
  // swapable -> indices. Suspend other Java threads without holding the mutator
  // lock: the runtime service refreshes its well-known JNI IDs itself.
  using namespace artdcevm::symbols;
  constexpr NamedSymbol instance{Library::kArt, "_ZN3art7Runtime9instance_E", SymbolTable::kAnyNamed};
  constexpr NamedSymbol change{Library::kArt, "_ZN3art7Runtime12SetJniIdTypeENS_9JniIdTypeE", SymbolTable::kAnyNamed};
  auto runtime = static_cast<void**>(Find(instance));
  auto promote = reinterpret_cast<void (*)(void*, int)>(Find(change));
  if (!runtime || !*runtime || !promote) return JVMTI_ERROR_NOT_AVAILABLE;
  JavaVM* vm = nullptr; jvmtiEnv* env = nullptr;
  if (jni->GetJavaVM(&vm) != JNI_OK || vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION_1_2) != JNI_OK)
    return JVMTI_ERROR_INVALID_ENVIRONMENT;
  jvmtiCapabilities caps{}; caps.can_suspend = 1;
  jvmtiError result = env->AddCapabilities(&caps);
  jthread self = nullptr; jthread* all = nullptr; jint count = 0;
  if (result == JVMTI_ERROR_NONE) result = env->GetCurrentThread(&self);
  if (result == JVMTI_ERROR_NONE) result = env->GetAllThreads(&count, &all);
  std::vector<jthread> others;
  if (result == JVMTI_ERROR_NONE)
    for (jint i = 0; i < count; ++i) if (!jni->IsSameObject(self, all[i])) others.push_back(all[i]);
  std::vector<jvmtiError> errors(others.size(), JVMTI_ERROR_INTERNAL);
  if (result == JVMTI_ERROR_NONE && !others.empty())
    result = env->SuspendThreadList(others.size(), others.data(), errors.data());
  std::vector<jthread> suspended;
  for (size_t i = 0; i < others.size(); ++i)
    if (errors[i] == JVMTI_ERROR_NONE) suspended.push_back(others[i]);
    else if (errors[i] != JVMTI_ERROR_THREAD_SUSPENDED && errors[i] != JVMTI_ERROR_THREAD_NOT_ALIVE) result = errors[i];
  if (result == JVMTI_ERROR_NONE) promote(*runtime, 1); // JniIdType::kIndices, enum class int.
  if (!suspended.empty()) {
    std::vector<jvmtiError> resumed(suspended.size());
    jvmtiError status = env->ResumeThreadList(suspended.size(), suspended.data(), resumed.data());
    if (status != JVMTI_ERROR_NONE) result = status;
    for (auto error : resumed) if (error != JVMTI_ERROR_NONE && error != JVMTI_ERROR_THREAD_NOT_ALIVE) result = error;
  }
  for (jint i = 0; i < count; ++i) jni->DeleteLocalRef(all[i]);
  if (all) env->Deallocate(reinterpret_cast<unsigned char*>(all));
  if (self) jni->DeleteLocalRef(self);
  env->DisposeEnvironment();
  return result;
}

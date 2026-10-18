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
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_validatedInterfaceGroups(JNIEnv*, jclass) {
  return counters[static_cast<int>(Counter::kValidatedGroups)];
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

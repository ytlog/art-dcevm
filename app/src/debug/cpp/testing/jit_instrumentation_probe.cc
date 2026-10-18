// Debug APK only. ART 13 named services and the leading declaration from:
// platform/art, android-13.0.0_r1, runtime/instrumentation.h and thread_list.h.
// No instruction decoding, address arithmetic on functions, or field writes.
#include <android/api-level.h>
#include <jni.h>
#include "jvmti.h"
#include "shadowhook.h"
#include "platform/abi/arm64/android13/thread.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
// Instrumentation has no base class or virtual members. These are its first
// data members, in source order. NeedsEntryExitHooksOffset() names the bool.
struct InstrumentationPrefix13 {
  uint64_t current_force_deopt_id_;
  bool instrumentation_stubs_installed_;
};
constexpr size_t NeedsEntryExitHooksOffset() {
  return offsetof(InstrumentationPrefix13, instrumentation_stubs_installed_);
}
// ScopedSuspendAll : ValueObject has no data members, bases with storage, or
// virtual members in the fixed source. Its named C1/D1 do all the work.
struct ScopedSuspendAll13 {};
// Continue the existing fixed Thread prefix through the two inline getters
// used by MaybeRestore (runtime/thread.h:714,1126). All fields are read-only.
using namespace artdcevm::abi::arm64::android13;
struct ThreadPointers13 {
  ThreadTlsPtr prefix;
  void* class_loader_override;
  void* long_jump_context;
  void* instrumentation_stack;
  void* stacked_shadow_frame_record;
  void* deoptimization_context_stack;
  void* frame_id_to_shadow_frame;
};
struct ThreadPrefix13 { ThreadTls32 tls32; ThreadTls64 tls64; ThreadPointers13 tls_ptr; };
struct InstrumentationFrame13 {
  void* this_object;
  void* method;
  uintptr_t return_pc;
  bool interpreter_entry;
  uint64_t force_deopt_id;
};
using InstrumentationStack13 = std::map<uintptr_t, InstrumentationFrame13>;
using ThreadCallback = void (*)(void*, void*);

void (*deopt_all)(void*) = nullptr;
void (*restore)(void*) = nullptr;
void (*restore_previous)(void*) = nullptr;
void (*for_each_previous)(void*, ThreadCallback, void*) = nullptr;
std::string (*pretty_method)(void*, bool) = nullptr;
void (*suspend_all)(ScopedSuspendAll13*, const char*, bool) = nullptr;
void (*resume_all)(ScopedSuspendAll13*) = nullptr;
int (*level)(const void*) = nullptr;
bool (*methods_empty)(const void*) = nullptr;
void* hook = nullptr;
void* restore_hook = nullptr;
void* for_each_hook = nullptr;
std::atomic<void*> instrumentation{nullptr};
std::atomic<jlong> before_flag{-1}, after_flag{-1}, before_id{-1}, after_id{-1};
std::atomic<jlong> attempts{0}, enabled_flag{-1}, step_events{0};
jvmtiEnv* probe_env = nullptr;
struct Blockers {
  bool collected = false;
  size_t threads = 0, old_frames = 0, forced_threads = 0, debugger_threads = 0;
  std::ostringstream details;
};
thread_local Blockers* collecting = nullptr;
thread_local std::string last_blockers;

template <typename T> T Read(const void* object, size_t offset) {
  T value;
  std::memcpy(&value, static_cast<const uint8_t*>(object) + offset, sizeof(value));
  return value;
}
int Flag(const void* object) { return Read<uint8_t>(object, NeedsEntryExitHooksOffset()); }
jlong Id(const void* object) {
  return static_cast<jlong>(Read<uint64_t>(object,
      offsetof(InstrumentationPrefix13, current_force_deopt_id_)));
}
void CaptureDeoptimization(void* object) {
  before_flag.store(Flag(object));
  before_id.store(Id(object));
  deopt_all(object);
  after_flag.store(Flag(object));
  after_id.store(Id(object));
  instrumentation.store(object);
}
struct InspectContext { ThreadCallback callback; void* argument; Blockers* blockers; };
void InspectThread(void* thread, void* argument) {
  auto& context = *static_cast<InspectContext*>(argument);
  auto& b = *context.blockers;
  size_t index = ++b.threads;
  // tlsPtr_.self starts null; only architectures that use this TLS slot need
  // to populate it. The initiating thread is verified with JNI/tid at install.
  void* declared_self = Read<void*>(thread, offsetof(ThreadPrefix13, tls_ptr.prefix.self));
  if (declared_self && declared_self != thread) {
    b.details << "thread-" << index << ": Thread prefix rejected\n";
  } else {
    uint32_t forced = Read<uint32_t>(thread, offsetof(ThreadPrefix13, tls32.force_interpreter_count));
    void* debugger = Read<void*>(thread, offsetof(ThreadPrefix13, tls_ptr.frame_id_to_shadow_frame));
    if (forced) ++b.forced_threads;
    if (debugger) ++b.debugger_threads;
    auto* stack = Read<const InstrumentationStack13*>(thread,
        offsetof(ThreadPrefix13, tls_ptr.instrumentation_stack));
    if (stack) for (const auto& entry : *stack) {
      const auto& frame = entry.second;
      if (frame.force_deopt_id != static_cast<uint64_t>(Id(instrumentation.load()))) {
        ++b.old_frames;
        // Only anonymous thread labels, method names and generation numbers.
        b.details << "thread-" << index << ": generation=" << frame.force_deopt_id
                  << " " << pretty_method(frame.method, true) << "\n";
      }
    }
  }
  context.callback(thread, context.argument);
}
void CaptureForEach(void* list, ThreadCallback callback, void* argument) {
  if (!collecting || collecting->collected) {
    for_each_previous(list, callback, argument);
    return;
  }
  collecting->collected = true;
  InspectContext context{callback, argument, collecting};
  // MaybeRestore holds thread_list_lock_ and the exclusive mutator lock here.
  for_each_previous(list, InspectThread, &context);
}
void CaptureRestore(void* object) {
  if (!instrumentation.load()) {
    restore_previous(object);
    return;
  }
  Blockers blockers;
  Blockers* previous = collecting;
  collecting = &blockers;
  restore_previous(object);
  collecting = previous;
  std::ostringstream report;
  report << "threads=" << blockers.threads << " oldFrames=" << blockers.old_frames
         << " forceInterpreterThreads=" << blockers.forced_threads
         << " debuggerShadowThreads=" << blockers.debugger_threads << "\n"
         << blockers.details.str();
  last_blockers = report.str();
}
void* Find(void* handle, const char* name) {
  // Local symbols in .symtab/.gnu_debugdata are looked up by their ELF name.
  return shadowhook_dlsym(handle, name);
}
struct Suspend {
  ScopedSuspendAll13 storage;
  Suspend() { suspend_all(&storage, "debug JIT instrumentation probe", false); }
  ~Suspend() { resume_all(&storage); }
};
void JNICALL SingleStep(jvmtiEnv*, JNIEnv*, jthread, jmethodID, jlocation) {
  step_events.fetch_add(1, std::memory_order_relaxed);
}
bool Recognized() {
  return instrumentation.load() && before_flag.load() == 0 && after_flag.load() == 1 &&
      after_id.load() == before_id.load() + 1;
}
}  // namespace

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  if (probe_env) return JNI_OK;
  if (vm->GetEnv(reinterpret_cast<void**>(&probe_env), JVMTI_VERSION_1_2) != JNI_OK)
    return JNI_ERR;
  jvmtiCapabilities capabilities{};
  capabilities.can_generate_single_step_events = 1;
  if (probe_env->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmtiEventCallbacks callbacks{};
  callbacks.SingleStep = SingleStep;
  return probe_env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE
      ? JNI_OK : JNI_ERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_JitInstrumentationProbe_install(JNIEnv* jni, jclass) {
  if (android_get_device_api_level() != 33) return -1;
  if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) return -2;
  void* handle = shadowhook_dlopen("libart.so");
  if (!handle) return -3;
  void* target = Find(handle,
      "_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv");
  restore = reinterpret_cast<decltype(restore)>(Find(handle,
      "_ZN3art15instrumentation15Instrumentation32MaybeRestoreInstrumentationStackEv"));
  suspend_all = reinterpret_cast<decltype(suspend_all)>(Find(handle,
      "_ZN3art16ScopedSuspendAllC1EPKcb"));
  resume_all = reinterpret_cast<decltype(resume_all)>(Find(handle,
      "_ZN3art16ScopedSuspendAllD1Ev"));
  level = reinterpret_cast<decltype(level)>(Find(handle,
      "_ZNK3art15instrumentation15Instrumentation30GetCurrentInstrumentationLevelEv"));
  methods_empty = reinterpret_cast<decltype(methods_empty)>(Find(handle,
      "_ZNK3art15instrumentation15Instrumentation25IsDeoptimizedMethodsEmptyEv"));
  pretty_method = reinterpret_cast<decltype(pretty_method)>(Find(handle,
      "_ZN3art9ArtMethod12PrettyMethodEb"));
  auto current = reinterpret_cast<void* (*)()>(Find(handle, "_ZN3art6Thread14CurrentFromGdbEv"));
  void* for_each = Find(handle, "_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");
  shadowhook_dlclose(handle);
  if (!target || !restore || !suspend_all || !resume_all || !level || !methods_empty ||
      !pretty_method || !current || !for_each) return -4;
  void* self = current();
  if (!self || Read<void*>(self, offsetof(ThreadPrefix13, tls_ptr.prefix.jni_env)) != jni ||
      Read<uint32_t>(self, offsetof(ThreadPrefix13, tls32.tid)) != syscall(__NR_gettid)) return -6;
  for_each_hook = shadowhook_hook_func_addr(for_each, reinterpret_cast<void*>(CaptureForEach),
                                           reinterpret_cast<void**>(&for_each_previous));
  restore_hook = shadowhook_hook_func_addr(reinterpret_cast<void*>(restore),
      reinterpret_cast<void*>(CaptureRestore), reinterpret_cast<void**>(&restore_previous));
  if (!for_each_hook || !restore_hook) return -7;
  hook = shadowhook_hook_func_addr(target, reinterpret_cast<void*>(CaptureDeoptimization),
                                  reinterpret_cast<void**>(&deopt_all));
  return hook ? 0 : -5;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_JitInstrumentationProbe_blockers(JNIEnv* jni, jclass) {
  return jni->NewStringUTF(last_blockers.c_str());
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_github_ytlog_artdcevm_testing_JitInstrumentationProbe_snapshot(JNIEnv* jni, jclass) {
  jlong values[] = {0, before_flag.load(), after_flag.load(), -1, before_id.load(),
                   after_id.load(), -1, -1, -1, attempts.load(), enabled_flag.load(), step_events.load()};
  if (void* object = instrumentation.load()) {
    Suspend suspend;
    values[0] = Recognized();
    values[3] = Flag(object);
    values[6] = Id(object);
    values[7] = level(object);
    values[8] = methods_empty(object);
  }
  jlongArray result = jni->NewLongArray(sizeof(values) / sizeof(values[0]));
  if (result) jni->SetLongArrayRegion(result, 0, sizeof(values) / sizeof(values[0]), values);
  return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_JitInstrumentationProbe_maybeRestore(JNIEnv*, jclass) {
  if (!Recognized()) return -1;
  Suspend suspend;
  void* object = instrumentation.load();
  // MaybeRestore itself does not check the instrumentation level. Its caller
  // must not remove active listener/global-interpreter instrumentation.
  if (level(object) != 0) return -2;
  attempts.fetch_add(1);
  restore(object);  // ART checks every thread, frame generation and deopt list.
  return Flag(object);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_JitInstrumentationProbe_cycleJvmti(JNIEnv*, jclass) {
  if (!probe_env || !Recognized()) return -1;
  jvmtiError enable = probe_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_SINGLE_STEP, nullptr);
  if (enable != JVMTI_ERROR_NONE) return enable;
  // Only native code runs on this thread between enable/disable. nullptr is
  // deliberately global: the per-thread ForceInterpreter path is different.
  {
    Suspend suspend;
    enabled_flag.store(Flag(instrumentation.load()));
  }
  return probe_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_SINGLE_STEP, nullptr);
}

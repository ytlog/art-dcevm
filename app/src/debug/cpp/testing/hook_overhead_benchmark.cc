// JNI loops for HookOverheadBenchmark. Each call goes through env->functions,
// so it measures whichever table is current: ART's own (CheckJNI or plain) or
// the engine's retirement-guard override in front of it.
#include <jni.h>
#include <time.h>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <unwind.h>
#include <cerrno>
#include <sched.h>

namespace {
jclass base_class = nullptr;
jmethodID value_id = nullptr, static_id = nullptr;
jfieldID field_id = nullptr;
jclass source_array = nullptr, target_array = nullptr;
}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_pinnedCpu(JNIEnv*, jclass) {
  cpu_set_t mask;
  if (sched_getaffinity(0, sizeof(mask), &mask) != 0) return -errno;
  const int cpu = sched_getcpu();
  return cpu >= 0 && CPU_COUNT(&mask) == 1 && CPU_ISSET(cpu, &mask) ? cpu : -1;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_pinCpu(JNIEnv* env, jclass type, jint cpu) {
  if (cpu < 0 || cpu >= CPU_SETSIZE) return -EINVAL;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  if (sched_setaffinity(0, sizeof(mask), &mask) != 0) return -errno;
  return Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_pinnedCpu(env, type);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_jniPrepare(JNIEnv* env, jclass, jclass base) {
  base_class = static_cast<jclass>(env->NewGlobalRef(base));
  value_id = env->GetMethodID(base, "value", "(I)I");
  static_id = env->GetStaticMethodID(base, "staticValue", "(I)I");
  field_id = env->GetFieldID(base, "field", "I");
  jclass source = env->FindClass("[[[Lcom/github/ytlog/artdcevm/fixture/HookBenchTypes$E0;");
  jclass target = env->FindClass("[[[Lcom/github/ytlog/artdcevm/fixture/HookBenchTypes$Base;");
  source_array = static_cast<jclass>(env->NewGlobalRef(source));
  target_array = static_cast<jclass>(env->NewGlobalRef(target));
  env->DeleteLocalRef(source);env->DeleteLocalRef(target);
  return base_class && value_id && static_id && field_id && source_array && target_array;
}

// kind 0: CallIntMethod, 1: CallStaticIntMethod, 2: GetIntField, 3: array covariance.
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_jniLoop(
    JNIEnv* env, jclass, jobject target, jint kind, jint count) {
  jlong sum = 0;
  switch (kind) {
    case 0: for (jint i = 0; i < count; ++i) sum += env->CallIntMethod(target, value_id, i); break;
    case 1: for (jint i = 0; i < count; ++i) sum += env->CallStaticIntMethod(base_class, static_id, i); break;
    case 2: for (jint i = 0; i < count; ++i) sum += env->GetIntField(target, field_id); break;
    case 3: for (jint i = 0; i < count; ++i) sum += env->IsAssignableFrom(source_array, target_array); break;
    default: return -1;
  }
  return sum;
}

// Exercise actual DWARF unwinding through interpreted callers and the native
// switch wrapper. Emit counts only; never retain or report native addresses.
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_interpreterUnwind(JNIEnv*, jclass) {
  struct Trace { unsigned frames = 0, engine = 0, after_engine = 0; } trace;
  _Unwind_Backtrace([](_Unwind_Context* context, void* data) {
    auto& result = *static_cast<Trace*>(data);
    ++result.frames;
    Dl_info info{};
    const auto pc = _Unwind_GetIP(context);
    if (pc && dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname &&
        std::strstr(info.dli_fname, "libartdcevm.so")) {
      ++result.engine;
      result.after_engine = 0;
    } else if (result.engine) ++result.after_engine;
    return _URC_NO_REASON;
  }, &trace);
  return (static_cast<jlong>(trace.frames) << 32) |
         (static_cast<jlong>(trace.engine) << 16) | trace.after_engine;
}


// Debug measurement setup only; calls the real workload through its ordinary
// JNI entry, so the warm-up caller cannot inline the measured Java entry.
extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_warmNative(
    JNIEnv* jni, jclass, jobject workload, jint count, jlong nanos) {
    jclass type = jni->GetObjectClass(workload);
    jmethodID run = type ? jni->GetMethodID(type, "run", "(I)J") : nullptr;
    jni->DeleteLocalRef(type);
    if (!run || jni->ExceptionCheck()) return;
    auto now = [] { timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; };
    const int64_t end = now() + nanos;
    uint32_t entries = 0;
    do {
        (void)jni->CallLongMethod(workload, run, count);
        if (jni->ExceptionCheck()) return;
        ++entries;
    } while (entries < 65536 || now() < end);
}

// Independent fixed-work clock proxy: only this debug library's register loop.
// No ART data, symbols, memory lookups or inferred machine addresses.
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_referenceWork(JNIEnv* jni,jclass type,jint expected_cpu) {
 if(expected_cpu>=0&&Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_pinnedCpu(jni,type)!=expected_cpu)return -1;
 auto cpu=[] { timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec; };
 // A multi-millisecond reference bounds relative clock-read noise on little
 // cores as well as big cores. Keep every observation, including outliers.
 uint64_t value=0x123456789abcdef0ULL;uint32_t count=4194304;int64_t begin=cpu();
 asm volatile(".p2align 6\n1: eor %0, %0, %0, lsl #7\neor %0, %0, %0, lsr #11\nadd %0, %0, #1\nsubs %w1, %w1, #1\nb.ne 1b\n" : "+r"(value),"+r"(count) : : "cc", "memory");
 int64_t elapsed=cpu()-begin;
 if(expected_cpu>=0&&Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_pinnedCpu(jni,type)!=expected_cpu)return -1;
 return elapsed;
}

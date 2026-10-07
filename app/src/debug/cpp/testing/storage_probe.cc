#include <jni.h>
#include <cstdarg>
namespace {
struct References {
  jobject object = nullptr;
  jclass cls = nullptr;
  bool flipped = false;
  jfieldID fields[4]{};
  jmethodID methods[3]{};
} generations[2];
jint InstanceNative(JNIEnv* env, jobject object, jint x) {
  auto cls = env->GetObjectClass(object);
  auto field = env->GetFieldID(cls, "anchor", "I");
  env->DeleteLocalRef(cls);
  return field ? env->GetIntField(object, field) + x + 500 : 0;
}
jint StaticNative(JNIEnv* env, jclass cls, jint x) {
  auto field = env->GetStaticFieldID(cls, "value", "I");
  return field ? env->GetStaticIntField(cls, field) + x + 600 : 0;
}
jlong Varargs(JNIEnv* env, const References& refs, jmethodID method, bool statik, int wide, ...) {
  va_list args; va_start(args, wide);
  jlong result = wide ? (statik ? env->CallStaticLongMethodV(refs.cls, method, args)
                               : env->CallLongMethodV(refs.object, method, args))
                      : (statik ? env->CallStaticIntMethodV(refs.cls, method, args)
                               : env->CallIntMethodV(refs.object, method, args));
  va_end(args); return result;
}
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_registerNative(
    JNIEnv* env, jclass, jclass cls, jboolean flipped) {
  JNINativeMethod method{"nativeFlip", "(I)I", flipped ? reinterpret_cast<void*>(StaticNative) : reinterpret_cast<void*>(InstanceNative)};
  env->RegisterNatives(cls, &method, 1);
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_retain(
    JNIEnv* env, jclass, jint generation, jobject object, jboolean flipped) {
  auto& refs = generations[generation];
  if (refs.object) env->DeleteGlobalRef(refs.object);
  if (refs.cls) env->DeleteGlobalRef(refs.cls);
  refs.object = env->NewGlobalRef(object);
  auto cls = env->GetObjectClass(object);
  refs.cls = static_cast<jclass>(env->NewGlobalRef(cls));
  refs.flipped = flipped;
  const char* names[]{"value", "count", "reference", "shared"};
  const char* signatures[]{"I", "J", "Ljava/lang/Object;", "Ljava/lang/Object;"};
  for (int i = 0; i < 4; ++i) {
    bool statik = (i % 2 == 0) == refs.flipped;
    refs.fields[i] = statik ? env->GetStaticFieldID(cls, names[i], signatures[i])
                            : env->GetFieldID(cls, names[i], signatures[i]);
    if (env->ExceptionCheck()) break;
  }
  const char* methods[]{"change", "alternate", "nativeFlip"};
  for (int i = 0; i < 3 && !env->ExceptionCheck(); ++i) {
    bool statik = (i != 1) == refs.flipped;
    const char* signature = i == 1 ? "(J)J" : "(I)I";
    refs.methods[i] = statik ? env->GetStaticMethodID(cls, methods[i], signature)
                             : env->GetMethodID(cls, methods[i], signature);
  }
  env->DeleteLocalRef(cls);
}
extern "C" JNIEXPORT jlong JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_field(
    JNIEnv* env, jclass, jint generation, jint index, jboolean write, jobject token) {
  const auto& refs = generations[generation];
  bool statik = (index % 2 == 0) == refs.flipped;
  auto id = refs.fields[index];
  if (write) {
    if (index == 0) { if (statik) env->SetStaticIntField(refs.cls, id, 45); else env->SetIntField(refs.object, id, 45); }
    else if (index == 1) { if (statik) env->SetStaticLongField(refs.cls, id, 9000000089LL); else env->SetLongField(refs.object, id, 9000000089LL); }
    else { if (statik) env->SetStaticObjectField(refs.cls, id, token); else env->SetObjectField(refs.object, id, token); }
    return 0;
  }
  if (index == 0) return statik ? env->GetStaticIntField(refs.cls, id) : env->GetIntField(refs.object, id);
  if (index == 1) return statik ? env->GetStaticLongField(refs.cls, id) : env->GetLongField(refs.object, id);
  auto value = statik ? env->GetStaticObjectField(refs.cls, id) : env->GetObjectField(refs.object, id);
  if (env->ExceptionCheck()) return 0;
  bool same = env->IsSameObject(value, token);
  env->DeleteLocalRef(value); return same;
}
extern "C" JNIEXPORT jlong JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_invoke(
    JNIEnv* env, jclass, jint generation, jint kind, jint mode) {
  const auto& refs = generations[generation];
  bool statik = (kind != 1) == refs.flipped;
  bool wide = kind == 1;
  auto method = refs.methods[kind];
  jvalue args[1]{};
  if (wide) args[0].j = 9000000007LL; else args[0].i = 4;
  switch (mode) {
    case 0: return wide ? (statik ? env->CallStaticLongMethod(refs.cls, method, args[0].j) : env->CallLongMethod(refs.object, method, args[0].j))
                        : (statik ? env->CallStaticIntMethod(refs.cls, method, args[0].i) : env->CallIntMethod(refs.object, method, args[0].i));
    case 1: return wide ? (statik ? env->CallStaticLongMethodA(refs.cls, method, args) : env->CallLongMethodA(refs.object, method, args))
                        : (statik ? env->CallStaticIntMethodA(refs.cls, method, args) : env->CallIntMethodA(refs.object, method, args));
    case 2: return wide ? Varargs(env, refs, method, statik, true, args[0].j) : Varargs(env, refs, method, statik, false, args[0].i);
    case 3: return wide ? env->CallNonvirtualLongMethodA(refs.object, refs.cls, method, args)
                        : env->CallNonvirtualIntMethodA(refs.object, refs.cls, method, args);
    default: return 0;
  }
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_release(JNIEnv* env, jclass) {
  for (auto& refs : generations) {
    if (refs.object) env->DeleteGlobalRef(refs.object);
    if (refs.cls) env->DeleteGlobalRef(refs.cls);
    refs = {};
  }
}

// Breakpoints exercise the switch interpreter, independently of nterp's cache.
#include "jvmti.h"
#include <atomic>
namespace {
jvmtiEnv* breakpoint_env = nullptr;
std::atomic<int> breakpoint_hits{0};
void JNICALL Hit(jvmtiEnv*, JNIEnv*, jthread, jmethodID, jlocation) { ++breakpoint_hits; }
jvmtiEnv* gc_env = nullptr;
std::atomic<int> finished_collections{0};
void JNICALL GcFinished(jvmtiEnv*) { ++finished_collections; }
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_gcStress(
    JNIEnv* env, jclass, jint operation) {
  if (operation == 0) {
    if (gc_env) return JVMTI_ERROR_DUPLICATE;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK ||
        vm->GetEnv(reinterpret_cast<void**>(&gc_env), JVMTI_VERSION_1_2) != JNI_OK)
      return JVMTI_ERROR_NOT_AVAILABLE;
    finished_collections.store(0);
    jvmtiCapabilities caps{}; caps.can_generate_garbage_collection_events = 1;
    auto error = gc_env->AddCapabilities(&caps);
    jvmtiEventCallbacks callbacks{}; callbacks.GarbageCollectionFinish = GcFinished;
    if (!error) error = gc_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (!error) error = gc_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, nullptr);
    if (error) { gc_env->DisposeEnvironment(); gc_env = nullptr; }
    return error;
  }
  if (!gc_env) return -JVMTI_ERROR_NOT_AVAILABLE;
  if (operation == 1) return gc_env->ForceGarbageCollection();
  if (operation != 2) return -JVMTI_ERROR_ILLEGAL_ARGUMENT;
  gc_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, nullptr);
  int count = finished_collections.load();
  gc_env->DisposeEnvironment(); gc_env = nullptr;
  return count;
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_breakpoint(
    JNIEnv* env, jclass, jobject method, jboolean enable) {
  auto id = env->FromReflectedMethod(method);
  if (!id) return JVMTI_ERROR_INVALID_METHODID;
  if (enable) {
    if (breakpoint_env) return JVMTI_ERROR_DUPLICATE;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK ||
        vm->GetEnv(reinterpret_cast<void**>(&breakpoint_env), JVMTI_VERSION_1_2) != JNI_OK)
      return JVMTI_ERROR_NOT_AVAILABLE;
    jvmtiCapabilities caps{}; caps.can_generate_breakpoint_events = 1;
    auto error = breakpoint_env->AddCapabilities(&caps);
    jvmtiEventCallbacks callbacks{}; callbacks.Breakpoint = Hit;
    if (!error) error = breakpoint_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (!error) error = breakpoint_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
    if (!error) error = breakpoint_env->SetBreakpoint(id, 0);
    if (error) { breakpoint_env->DisposeEnvironment(); breakpoint_env = nullptr; }
    return error;
  }
  if (!breakpoint_env) return JVMTI_ERROR_NOT_AVAILABLE;
  auto error = breakpoint_env->ClearBreakpoint(id, 0);
  breakpoint_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
  breakpoint_env->DisposeEnvironment(); breakpoint_env = nullptr;
  return error;
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_StorageProbe_breakpointHits(JNIEnv*, jclass) {
  return breakpoint_hits.load();
}

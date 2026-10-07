#include <jni.h>
#include <cstdarg>
namespace {
jobject retained = nullptr;
jclass retained_class = nullptr;
jmethodID wide = nullptr, reference = nullptr, static_reference = nullptr;
jlong NativeWide(JNIEnv*, jclass, jlong x, jdouble y, jobject token) {
  return x + static_cast<jlong>(y) + (token ? 101 : 100);
}
jobject NativeReference(JNIEnv* env, jobject, jobject token) { return env->NewLocalRef(token); }
jlong WideV(JNIEnv* env, jclass cls, jmethodID method, ...) {
  va_list args; va_start(args, method);
  jlong result = env->CallStaticLongMethodV(cls, method, args);
  va_end(args); return result;
}
jobject ReferenceV(JNIEnv* env, bool statik, jmethodID method, ...) {
  va_list args; va_start(args, method);
  jobject result = statik ? env->CallStaticObjectMethodV(retained_class, method, args)
                          : env->CallObjectMethodV(retained, method, args);
  va_end(args); return result;
}
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_NativeShapeProbe_register(
    JNIEnv* env, jclass, jclass cls) {
  JNINativeMethod methods[] = {
    {"wide", "(JDLjava/lang/Object;)J", reinterpret_cast<void*>(NativeWide)},
    {"reference", "(Ljava/lang/Object;)Ljava/lang/Object;", reinterpret_cast<void*>(NativeReference)},
    {"staticReference", "(Ljava/lang/Object;)Ljava/lang/Object;", reinterpret_cast<void*>(NativeReference)},
  };
  env->RegisterNatives(cls, methods, 3);
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_NativeShapeProbe_retain(
    JNIEnv* env, jclass, jobject object) {
  if (retained) env->DeleteGlobalRef(retained);
  if (retained_class) env->DeleteGlobalRef(retained_class);
  retained = env->NewGlobalRef(object);
  auto cls = env->GetObjectClass(object);
  retained_class = static_cast<jclass>(env->NewGlobalRef(cls));
  wide = env->GetStaticMethodID(cls, "wide", "(JDLjava/lang/Object;)J");
  reference = env->GetMethodID(cls, "reference", "(Ljava/lang/Object;)Ljava/lang/Object;");
  static_reference = env->GetStaticMethodID(cls, "staticReference", "(Ljava/lang/Object;)Ljava/lang/Object;");
  env->DeleteLocalRef(cls);
}
extern "C" JNIEXPORT jlong JNICALL Java_com_github_ytlog_artdcevm_testing_NativeShapeProbe_wide(
    JNIEnv* env, jclass, jint mode, jlong x, jdouble y, jobject token) {
  jvalue args[3]{}; args[0].j = x; args[1].d = y; args[2].l = token;
  switch (mode) {
    case 0: return env->CallStaticLongMethod(retained_class, wide, x, y, token);
    case 1: return env->CallStaticLongMethodA(retained_class, wide, args);
    default: return WideV(env, retained_class, wide, x, y, token);
  }
}
extern "C" JNIEXPORT jobject JNICALL Java_com_github_ytlog_artdcevm_testing_NativeShapeProbe_reference(
    JNIEnv* env, jclass, jint mode, jboolean statik, jobject token) {
  auto method = statik ? static_reference : reference;
  jvalue args[1]{}; args[0].l = token;
  switch (mode) {
    case 0: return statik ? env->CallStaticObjectMethod(retained_class, method, token)
                          : env->CallObjectMethod(retained, method, token);
    case 1: return statik ? env->CallStaticObjectMethodA(retained_class, method, args)
                          : env->CallObjectMethodA(retained, method, args);
    default: return ReferenceV(env, statik, method, token);
  }
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_NativeShapeProbe_release(JNIEnv* env, jclass) {
  if (retained) env->DeleteGlobalRef(retained);
  if (retained_class) env->DeleteGlobalRef(retained_class);
  retained = nullptr; retained_class = nullptr;
  wide = reference = static_reference = nullptr;
}

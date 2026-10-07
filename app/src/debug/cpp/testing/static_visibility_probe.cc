#include <jni.h>
#include <cstdarg>

namespace {
jclass retained_class = nullptr;
jmethodID retained_method = nullptr;

jint InvokeV(JNIEnv* env, jclass type, jmethodID method, ...) {
  va_list args;
  va_start(args, method);
  jint value = env->CallStaticIntMethodV(type, method, args);
  va_end(args);
  return value;
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_StaticVisibilityProbe_retain(JNIEnv* env, jclass, jclass type) {
  if (retained_class != nullptr) env->DeleteGlobalRef(retained_class);
  retained_class = static_cast<jclass>(env->NewGlobalRef(type));
  if (retained_class == nullptr) return JNI_FALSE;
  retained_method = env->GetStaticMethodID(retained_class, "compute", "()I");
  return retained_method != nullptr ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StaticVisibilityProbe_invoke(JNIEnv* env, jclass,
                                                        jint mode, jboolean fresh) {
  if (retained_class == nullptr) return -1;
  jmethodID method = fresh ? env->GetStaticMethodID(retained_class, "compute", "()I")
                           : retained_method;
  if (method == nullptr) return -1;
  switch (mode) {
    case 0: return env->CallStaticIntMethod(retained_class, method);
    case 1: return env->CallStaticIntMethodA(retained_class, method, nullptr);
    case 2: return InvokeV(env, retained_class, method);
    default: return -1;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StaticVisibilityProbe_release(JNIEnv* env, jclass) {
  if (retained_class != nullptr) env->DeleteGlobalRef(retained_class);
  retained_class = nullptr;
  retained_method = nullptr;
}

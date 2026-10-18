// JNI loops for HookOverheadBenchmark. Each call goes through env->functions,
// so it measures whichever table is current: ART's own (CheckJNI or plain) or
// the engine's retirement-guard override in front of it.
#include <jni.h>

namespace {
jclass base_class = nullptr;
jmethodID value_id = nullptr, static_id = nullptr;
jfieldID field_id = nullptr;
}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_jniPrepare(JNIEnv* env, jclass, jclass base) {
  base_class = static_cast<jclass>(env->NewGlobalRef(base));
  value_id = env->GetMethodID(base, "value", "(I)I");
  static_id = env->GetStaticMethodID(base, "staticValue", "(I)I");
  field_id = env->GetFieldID(base, "field", "I");
  return base_class && value_id && static_id && field_id;
}

// kind 0: CallIntMethod, 1: CallStaticIntMethod, 2: GetIntField.
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_HookOverheadBenchmark_jniLoop(
    JNIEnv* env, jclass, jobject target, jint kind, jint count) {
  jlong sum = 0;
  switch (kind) {
    case 0: for (jint i = 0; i < count; ++i) sum += env->CallIntMethod(target, value_id, i); break;
    case 1: for (jint i = 0; i < count; ++i) sum += env->CallStaticIntMethod(base_class, static_id, i); break;
    case 2: for (jint i = 0; i < count; ++i) sum += env->GetIntField(target, field_id); break;
    default: return -1;
  }
  return sum;
}

#include <jni.h>
#include <cstdarg>
namespace {
jobject retained = nullptr;
jmethodID method_id = nullptr;
jfieldID field_id = nullptr;
jobject removal_receiver = nullptr;
jclass removal_class = nullptr;
jmethodID removal_method = nullptr;
jobject fields_receiver = nullptr;
jclass fields_class = nullptr;
jfieldID cached_fields[18]{};
void Throw(JNIEnv* env, const char* message) { env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), message); }
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_retainFixture(JNIEnv* env, jclass, jobject obj) {
  if (retained) { env->DeleteGlobalRef(retained); retained = nullptr; }
  auto cls = env->GetObjectClass(obj);
  method_id = env->GetMethodID(cls, "compute", "()I");
  field_id = env->GetFieldID(cls, "value", "I");
  if (!env->ExceptionCheck()) retained = env->NewGlobalRef(obj);
  env->DeleteLocalRef(cls);
}
extern "C" JNIEXPORT jintArray JNICALL Java_dev_artdcevm_testing_NativeTestBridge_inspectRetainedFixture(JNIEnv* env, jclass) {
  if (!retained || !method_id || !field_id) { Throw(env, "No cached JNI fixture"); return nullptr; }
  jint values[2] = {env->CallIntMethod(retained, method_id), 0};
  if (env->ExceptionCheck()) return nullptr;
  values[1] = env->GetIntField(retained, field_id);
  if (env->ExceptionCheck()) return nullptr;
  auto out = env->NewIntArray(2);
  if (out) env->SetIntArrayRegion(out, 0, 2, values);
  return out;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_releaseFixture(JNIEnv* env, jclass) {
  if (retained) env->DeleteGlobalRef(retained);
  retained = nullptr; method_id = nullptr; field_id = nullptr;
  if (removal_receiver) env->DeleteGlobalRef(removal_receiver);
  if (removal_class) env->DeleteGlobalRef(removal_class);
  removal_receiver = nullptr; removal_class = nullptr; removal_method = nullptr;
  if (fields_receiver) env->DeleteGlobalRef(fields_receiver);
  if (fields_class) env->DeleteGlobalRef(fields_class);
  fields_receiver = nullptr; fields_class = nullptr;
  for (auto& id : cached_fields) id = nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_retainRemovalMethod(
    JNIEnv* env, jclass, jobject instance) {
  if (removal_receiver) env->DeleteGlobalRef(removal_receiver);
  if (removal_class) env->DeleteGlobalRef(removal_class);
  removal_receiver = env->NewGlobalRef(instance);
  jclass cls = env->GetObjectClass(instance);
  removal_class = static_cast<jclass>(env->NewGlobalRef(cls));
  removal_method = env->GetMethodID(cls, "addedMethod", "()I");
  env->DeleteLocalRef(cls);
}
namespace {
jint InvokeVarargs(JNIEnv* env, jobject receiver, jmethodID method, ...) {
  va_list args;
  va_start(args, method);
  jint result = env->CallIntMethodV(receiver, method, args);
  va_end(args);
  return result;
}
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_NativeTestBridge_invokeRemovalMethod(
    JNIEnv* env, jclass, jint mode) {
  if (!removal_receiver || !removal_method) { Throw(env, "No cached removal method"); return 0; }
  switch (mode) {
    case 0: return env->CallIntMethod(removal_receiver, removal_method);
    case 1: return env->CallIntMethodA(removal_receiver, removal_method, nullptr);
    case 2: return InvokeVarargs(env, removal_receiver, removal_method);
    case 3: return env->CallNonvirtualIntMethodA(removal_receiver, removal_class, removal_method, nullptr);
    default: Throw(env, "Unknown JNI invocation mode"); return 0;
  }
}

extern "C" JNIEXPORT jlong JNICALL Java_dev_artdcevm_testing_NativeTestBridge_invokeSignatureMethod(
    JNIEnv* env, jclass, jobject instance, jlong argument) {
  jclass cls = env->GetObjectClass(instance);
  jmethodID method = env->GetMethodID(cls, "addedMethod", "(J)J");
  env->DeleteLocalRef(cls);
  if (!method || env->ExceptionCheck()) return 0;
  return env->CallLongMethod(instance, method, argument);
}

extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_NativeTestBridge_retainFields(
    JNIEnv* env, jclass, jobject instance) {
  if (fields_receiver) env->DeleteGlobalRef(fields_receiver);
  if (fields_class) env->DeleteGlobalRef(fields_class);
  fields_receiver = env->NewGlobalRef(instance);
  jclass cls = env->GetObjectClass(instance);
  fields_class = static_cast<jclass>(env->NewGlobalRef(cls));
  const char* names[] = {"z", "b", "c", "s", "i", "j", "f", "d", "o",
                         "sz", "sb", "sc", "ss", "si", "sj", "sf", "sd", "so"};
  const char* types[] = {"Z", "B", "C", "S", "I", "J", "F", "D", "Ljava/lang/Object;"};
  for (int i = 0; i < 18 && !env->ExceptionCheck(); ++i) {
    cached_fields[i] = i < 9 ? env->GetFieldID(cls, names[i], types[i])
                            : env->GetStaticFieldID(cls, names[i], types[i % 9]);
  }
  env->DeleteLocalRef(cls);
}
extern "C" JNIEXPORT jdouble JNICALL Java_dev_artdcevm_testing_NativeTestBridge_accessField(
    JNIEnv* env, jclass, jint index, jboolean write) {
  if (index < 0 || index >= 18 || !fields_receiver || !cached_fields[index]) {
    Throw(env, "Invalid cached field"); return 0;
  }
  auto id = cached_fields[index];
#define FIELD_CASE(INDEX, TYPE) \
  case INDEX: \
    if (write) { \
      if (index < 9) env->Set##TYPE##Field(fields_receiver, id, 0); \
      else env->SetStatic##TYPE##Field(fields_class, id, 0); \
      return 0; \
    } \
    return index < 9 ? env->Get##TYPE##Field(fields_receiver, id) \
                     : env->GetStatic##TYPE##Field(fields_class, id)
  switch (index % 9) {
    FIELD_CASE(0, Boolean); FIELD_CASE(1, Byte); FIELD_CASE(2, Char); FIELD_CASE(3, Short);
    FIELD_CASE(4, Int); FIELD_CASE(5, Long); FIELD_CASE(6, Float); FIELD_CASE(7, Double);
    case 8:
      if (write) {
        if (index < 9) env->SetObjectField(fields_receiver, id, nullptr);
        else env->SetStaticObjectField(fields_class, id, nullptr);
        return 0;
      } else {
        jobject object = index < 9 ? env->GetObjectField(fields_receiver, id)
                                   : env->GetStaticObjectField(fields_class, id);
        if (env->ExceptionCheck()) return 0;
        if (object) env->DeleteLocalRef(object);
        return object ? 1 : 0;
      }
    default: return 0;
  }
#undef FIELD_CASE
}
extern "C" JNIEXPORT jlong JNICALL Java_dev_artdcevm_testing_NativeTestBridge_accessChangedLongField(
    JNIEnv* env, jclass, jobject instance, jlong value) {
  jclass cls = env->GetObjectClass(instance);
  jfieldID id = env->GetFieldID(cls, "i", "J");
  env->DeleteLocalRef(cls);
  if (!id || env->ExceptionCheck()) return 0;
  env->SetLongField(instance, id, value);
  if (env->ExceptionCheck()) return 0;
  return env->GetLongField(instance, id);
}

#include <jni.h>
#include <cstdarg>
namespace {
jobject object = nullptr;
jclass base = nullptr;
jfieldID root_field = nullptr, base_field = nullptr, own_field = nullptr;
jmethodID inherited = nullptr;
jclass cached_interface = nullptr;
jmethodID cached_interface_method = nullptr;
jclass cached_default_interface = nullptr;
jmethodID cached_default_method = nullptr;
jint CallIntMethodV(JNIEnv* env, jobject receiver, jmethodID method, ...) {
  va_list args;
  va_start(args, method);
  jint result = env->CallIntMethodV(receiver, method, args);
  va_end(args);
  return result;
}
jint CallNonvirtualIntMethodV(JNIEnv* env, jobject receiver, jclass parent,
                             jmethodID method, ...) {
  va_list args;
  va_start(args, method);
  jint result = env->CallNonvirtualIntMethodV(receiver, parent, method, args);
  va_end(args);
  return result;
}
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_retain(JNIEnv* env, jclass, jobject value) {
  object = env->NewGlobalRef(value);
  auto cls = env->GetObjectClass(value);
  root_field = env->GetFieldID(cls, "root", "J");
  base_field = env->GetFieldID(cls, "base", "I");
  own_field = env->GetFieldID(cls, "value", "I");
  env->DeleteLocalRef(cls);
  auto local_base = env->FindClass("dev/artdcevm/fixture/HierarchyBase");
  base = static_cast<jclass>(env->NewGlobalRef(local_base));
  inherited = env->GetMethodID(base, "inherited", "()I");
  env->DeleteLocalRef(local_base);
}
extern "C" JNIEXPORT jlongArray JNICALL Java_dev_artdcevm_testing_HierarchyProbe_fields(JNIEnv* env, jclass) {
  jlong values[]{env->GetLongField(object, root_field), env->GetIntField(object, base_field), env->GetIntField(object, own_field)};
  auto result = env->NewLongArray(3);
  if (result) env->SetLongArrayRegion(result, 0, 3, values);
  return result;
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_invoke(JNIEnv* env, jclass, jboolean nonvirtual) {
  return nonvirtual ? env->CallNonvirtualIntMethodA(object, base, inherited, nullptr)
                    : env->CallIntMethodA(object, inherited, nullptr);
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_artdcevm_testing_HierarchyProbe_instanceOf(JNIEnv* env, jclass, jclass cls) {
  return env->IsInstanceOf(object, cls);
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_narrowJniRead(
    JNIEnv* env, jclass, jobject receiver) {
  jclass parent = env->FindClass("dev/artdcevm/fixture/NarrowParent");
  if (parent == nullptr) return 0;
  jfieldID field = env->GetFieldID(parent, "ancestor", "I");
  env->DeleteLocalRef(parent);
  return field == nullptr ? 0 : env->GetIntField(receiver, field);
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_narrowJniInvoke(
    JNIEnv* env, jclass, jobject receiver) {
  jclass parent = env->FindClass("dev/artdcevm/fixture/NarrowParent");
  if (parent == nullptr) return 0;
  jmethodID method = env->GetMethodID(parent, "inherited", "()I");
  env->DeleteLocalRef(parent);
  return method == nullptr ? 0 : env->CallIntMethodA(receiver, method, nullptr);
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_narrowJniInvokeNonvirtual(
    JNIEnv* env, jclass, jobject receiver) {
  jclass parent = env->FindClass("dev/artdcevm/fixture/NarrowParent");
  if (parent == nullptr) return 0;
  jmethodID method = env->GetMethodID(parent, "inherited", "()I");
  if (method == nullptr) { env->DeleteLocalRef(parent); return 0; }
  jint value = env->CallNonvirtualIntMethodA(receiver, parent, method, nullptr);
  env->DeleteLocalRef(parent);
  return value;
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_narrowJniInvokeForm(
    JNIEnv* env, jclass, jobject receiver, jint form) {
  jclass parent = env->FindClass("dev/artdcevm/fixture/NarrowParent");
  if (parent == nullptr) return 0;
  jmethodID method = env->GetMethodID(parent, "inherited", "()I");
  if (method == nullptr) { env->DeleteLocalRef(parent); return 0; }
  jint result = 0;
  switch (form) {
    case 0: result = env->CallIntMethod(receiver, method); break;
    case 1: result = CallIntMethodV(env, receiver, method); break;
    case 2: result = env->CallNonvirtualIntMethod(receiver, parent, method); break;
    case 3: result = CallNonvirtualIntMethodV(env, receiver, parent, method); break;
    default: env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Unknown JNI call form");
  }
  env->DeleteLocalRef(parent);
  return result;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_retainInterfaceMethod(
    JNIEnv* env, jclass, jclass type) {
  if (cached_interface != nullptr) env->DeleteGlobalRef(cached_interface);
  cached_interface = static_cast<jclass>(env->NewGlobalRef(type));
  cached_interface_method = cached_interface == nullptr ? nullptr
      : env->GetMethodID(cached_interface, "tag", "()I");
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_interfaceJniInvoke(
    JNIEnv* env, jclass, jobject receiver) {
  if (cached_interface_method == nullptr) return 0;
  return env->CallIntMethodA(receiver, cached_interface_method, nullptr);
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_interfaceJniInvokeForm(
    JNIEnv* env, jclass, jobject receiver, jint form, jboolean fresh) {
  jclass type = nullptr;
  jmethodID method = cached_interface_method;
  if (fresh == JNI_TRUE) {
    type = env->FindClass("dev/artdcevm/fixture/InterfaceTag");
    if (type == nullptr) return 0;
    method = env->GetMethodID(type, "tag", "()I");
  }
  if (method == nullptr) {
    if (type != nullptr) env->DeleteLocalRef(type);
    return 0;
  }
  jint result = 0;
  switch (form) {
    case 0: result = env->CallIntMethod(receiver, method); break;
    case 1: result = env->CallIntMethodA(receiver, method, nullptr); break;
    case 2: result = CallIntMethodV(env, receiver, method); break;
    default: env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Unknown JNI interface call form");
  }
  if (type != nullptr) env->DeleteLocalRef(type);
  return result;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_releaseInterfaceMethod(
    JNIEnv* env, jclass) {
  if (cached_interface != nullptr) env->DeleteGlobalRef(cached_interface);
  cached_interface = nullptr;
  cached_interface_method = nullptr;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_retainDefaultInterfaceMethod(
    JNIEnv* env, jclass, jclass type) {
  if (cached_default_interface != nullptr) env->DeleteGlobalRef(cached_default_interface);
  cached_default_interface = static_cast<jclass>(env->NewGlobalRef(type));
  cached_default_method = cached_default_interface == nullptr ? nullptr
      : env->GetMethodID(cached_default_interface, "defaultValue", "()I");
}
extern "C" JNIEXPORT jint JNICALL Java_dev_artdcevm_testing_HierarchyProbe_defaultInterfaceJniInvokeForm(
    JNIEnv* env, jclass, jobject receiver, jint form, jboolean fresh) {
  jclass type = nullptr;
  jmethodID method = cached_default_method;
  jclass declaring = cached_default_interface;
  if (fresh == JNI_TRUE) {
    type = env->FindClass("dev/artdcevm/fixture/InterfaceTag");
    if (type == nullptr) return 0;
    declaring = type;
    method = env->GetMethodID(type, "defaultValue", "()I");
  }
  if (method == nullptr) {
    if (type != nullptr) env->DeleteLocalRef(type);
    return 0;
  }
  jint result = 0;
  switch (form) {
    case 0: result = env->CallIntMethodA(receiver, method, nullptr); break;
    case 1: result = env->CallNonvirtualIntMethodA(receiver, declaring, method, nullptr); break;
    case 2: result = env->CallNonvirtualIntMethod(receiver, declaring, method); break;
    case 3: result = CallNonvirtualIntMethodV(env, receiver, declaring, method); break;
    default: env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Unknown JNI default call form");
  }
  if (type != nullptr) env->DeleteLocalRef(type);
  return result;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_releaseDefaultInterfaceMethod(
    JNIEnv* env, jclass) {
  if (cached_default_interface != nullptr) env->DeleteGlobalRef(cached_default_interface);
  cached_default_interface = nullptr;
  cached_default_method = nullptr;
}
extern "C" JNIEXPORT void JNICALL Java_dev_artdcevm_testing_HierarchyProbe_release(JNIEnv* env, jclass) {
  if (object) env->DeleteGlobalRef(object);
  if (base) env->DeleteGlobalRef(base);
  object = nullptr; base = nullptr;
  root_field = base_field = own_field = nullptr; inherited = nullptr;
}

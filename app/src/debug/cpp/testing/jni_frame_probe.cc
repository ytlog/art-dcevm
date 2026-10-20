#include <jni.h>
#include <cstdarg>
#include <cstdint>
#include <atomic>

namespace {
jclass owner = nullptr, gate = nullptr;
jmethodID cached = nullptr, pause_method = nullptr;
std::atomic<bool> anchors_ready{false};
jboolean (JNICALL *main_check)(JNIEnv*) = nullptr;
jobject (JNICALL *main_local)(JNIEnv*, jobject) = nullptr;
jint (JNICALL *main_call)(JNIEnv*, jobject, jmethodID, const jvalue*) = nullptr;
void Fail(JNIEnv* jni, const char* message) {
  jclass type = jni->FindClass("java/lang/AssertionError");
  if (type) { jni->ThrowNew(type, message); jni->DeleteLocalRef(type); }
}
jint VarArgs(JNIEnv* jni, jobject receiver, jclass retained_owner, bool nonvirtual,
             jmethodID method, ...) {
  va_list args;
  va_start(args, method);
  const jint result = nonvirtual ? jni->CallNonvirtualIntMethodV(receiver, retained_owner, method, args)
                                : jni->CallIntMethodV(receiver, method, args);
  va_end(args);
  return result;
}
}  // namespace
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_fixture_JniFrameProbe_cache(JNIEnv* jni, jclass, jclass type) {
  anchors_ready.store(false, std::memory_order_release);
  if (owner) jni->DeleteGlobalRef(owner);
  if (gate) jni->DeleteGlobalRef(gate);
  owner = static_cast<jclass>(jni->NewGlobalRef(type));
  if (!owner) return -1;
  jclass gate_type = jni->FindClass("com/github/ytlog/artdcevm/fixture/JniFrameGate");
  gate = gate_type ? static_cast<jclass>(jni->NewGlobalRef(gate_type)) : nullptr;
  if (gate_type) jni->DeleteLocalRef(gate_type);
  if (!owner || !gate) return -1;
  pause_method = jni->GetStaticMethodID(gate, "pause", "(I)I");
  if (!pause_method) return -1;
  cached = jni->GetMethodID(owner, "value", "()I");
  return cached ? static_cast<jint>(reinterpret_cast<uintptr_t>(cached) & 1u) : -1;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_fixture_JniFrameProbe_prepareThreadPolicy(JNIEnv* jni, jclass) {
  main_check = jni->functions->ExceptionCheck;
  main_local = jni->functions->NewLocalRef;
  main_call = jni->functions->CallIntMethodA;
  const bool valid = cached && owner && gate && main_check && main_local && main_call;
  anchors_ready.store(valid, std::memory_order_release);
  return valid;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_fixture_JniFrameProbe_invoke(
    JNIEnv* jni, jclass, jobject receiver, jboolean nonvirtual, jint form, jboolean hold_native) {
  // Compare public JNI slots with the configured instrumentation thread. This
  // proves a worker inherits the same checking policy without reading ART TLS.
  if (!anchors_ready.load(std::memory_order_acquire) ||
      jni->functions->ExceptionCheck != main_check || jni->functions->NewLocalRef != main_local ||
      jni->functions->CallIntMethodA != main_call) {
    Fail(jni, "Worker JNI policy differs from the configured thread");
    return 0;
  }
  if (form < 0 || form > 2) { Fail(jni, "Invalid JNI argument form"); return 0; }
  // Save all three before the gate. ART owns the local roots; the ID remains an
  // opaque value in this native frame across every transaction and collection.
  jobject retained = jni->NewLocalRef(receiver);
  if (!retained) { if (!jni->ExceptionCheck()) Fail(jni, "Missing JNI receiver root"); return 0; }
  jclass retained_owner = static_cast<jclass>(jni->NewLocalRef(owner));
  const jmethodID method = cached;
  if (!retained || !retained_owner || !method) { if (!jni->ExceptionCheck()) Fail(jni, "Missing JNI frame roots"); return 0; }
  if (hold_native) {
    jni->CallStaticIntMethod(gate, pause_method, 0);
    if (jni->ExceptionCheck()) return 0;
  }
  if (!jni->IsSameObject(receiver, retained) || !jni->IsSameObject(owner, retained_owner)) {
    Fail(jni, "Held JNI local identity changed");
    return 0;
  }
  jint result = 0;
  if (form == 1) result = VarArgs(jni, retained, retained_owner, nonvirtual, method);
  else if (form == 2) result = nonvirtual ? jni->CallNonvirtualIntMethodA(retained, retained_owner, method, nullptr)
                                         : jni->CallIntMethodA(retained, method, nullptr);
  else result = nonvirtual ? jni->CallNonvirtualIntMethod(retained, retained_owner, method)
                           : jni->CallIntMethod(retained, method);
  if (!jni->ExceptionCheck() &&
      (!jni->IsSameObject(receiver, retained) || !jni->IsSameObject(owner, retained_owner)))
    Fail(jni, "Returned JNI local identity changed");
  return result;
}

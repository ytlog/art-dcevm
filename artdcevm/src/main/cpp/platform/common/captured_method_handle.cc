#include "platform/common/captured_method_handle.h"
#include <cstring>
#include <type_traits>

namespace artdcevm::captured_method_handle {
namespace {
JavaVM* vm = nullptr;
jvmtiEnv* environment = nullptr;
jmethodID transform = nullptr;
jint argument_slot = -1;
jclass frame_class = nullptr, transformer_class = nullptr, internal_class = nullptr;
jfieldID delegate_field = nullptr, receiver_field = nullptr, range_field = nullptr;
jfieldID type_field = nullptr, references_field = nullptr;
jmethodID create = nullptr, copy_range = nullptr, invoke = nullptr, copy_return = nullptr;
template <bool method> auto Member(jvmtiEnv* env, jclass klass, const char* name, const char* signature) {
  using Id = std::conditional_t<method, jmethodID, jfieldID>;
  jint count = 0;
  Id* members = nullptr;
  Id found = nullptr;
  jvmtiError result;
  if constexpr (method) result = env->GetClassMethods(klass, &count, &members);
  else result = env->GetClassFields(klass, &count, &members);
  if (result) return found;
  for (jint i = 0; i < count; ++i) {
    char* n = nullptr; char* s = nullptr;
    if constexpr (method) result = env->GetMethodName(members[i], &n, &s, nullptr);
    else result = env->GetFieldName(klass, members[i], &n, &s, nullptr);
    if (!result && n && s && !std::strcmp(n, name) && !std::strcmp(s, signature)) found = members[i];
    if (n) env->Deallocate(reinterpret_cast<unsigned char*>(n));
    if (s) env->Deallocate(reinterpret_cast<unsigned char*>(s));
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(members));
  return found;
}
jclass Global(JNIEnv* jni, const char* name) {
  auto local = jni->FindClass(name);
  return local ? static_cast<jclass>(jni->NewGlobalRef(local)) : nullptr;
}
bool Fail(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(internal_class, "Captured MethodHandle continuation failed");
  return false;
}
}
bool Prepare(JNIEnv* jni, jvmtiEnv* env, jmethodID entry) {
  if (transform) return transform == entry;
  jclass bound = jni->FindClass("java/lang/invoke/Transformers$BindTo");
  if (!bound || jni->ExceptionCheck()) return false;
  jclass handle = jni->FindClass("java/lang/invoke/MethodHandle");
  if (!handle || jni->ExceptionCheck()) return false;
  if (!frame_class) frame_class = Global(jni, "dalvik/system/EmulatedStackFrame");
  if (!frame_class || jni->ExceptionCheck()) return false;
  if (!transformer_class) transformer_class = Global(jni, "java/lang/invoke/Transformers$Transformer");
  if (!transformer_class || jni->ExceptionCheck()) return false;
  if (!internal_class) internal_class = Global(jni, "java/lang/InternalError");
  if (!internal_class || jni->ExceptionCheck()) return false;
  delegate_field = Member<false>(env, bound, "delegate", "Ljava/lang/invoke/MethodHandle;");
  receiver_field = Member<false>(env, bound, "receiver", "Ljava/lang/Object;");
  range_field = Member<false>(env, bound, "range", "Ldalvik/system/EmulatedStackFrame$Range;");
  type_field = Member<false>(env, handle, "type", "Ljava/lang/invoke/MethodType;");
  references_field = Member<false>(env, frame_class, "references", "[Ljava/lang/Object;");
  create = Member<true>(env, frame_class, "create", "(Ljava/lang/invoke/MethodType;)Ldalvik/system/EmulatedStackFrame;");
  copy_range = Member<true>(env, frame_class, "copyRangeTo", "(Ldalvik/system/EmulatedStackFrame;Ldalvik/system/EmulatedStackFrame$Range;II)V");
  copy_return = Member<true>(env, frame_class, "copyReturnValueTo", "(Ldalvik/system/EmulatedStackFrame;)V");
  invoke = Member<true>(env, transformer_class, "invokeFromTransform", "(Ljava/lang/invoke/MethodHandle;Ldalvik/system/EmulatedStackFrame;)V");
  jint locals = 0;
  if (jni->ExceptionCheck() || !delegate_field || !receiver_field || !range_field || !type_field || !references_field ||
      !create || !copy_range || !copy_return || !invoke || env->GetMaxLocals(entry, &locals) || locals < 2 ||
      jni->GetJavaVM(&vm)) return false;
  environment = env;
  argument_slot = locals - 1;  // DEX input registers end the register file; transform has two references.
  transform = entry;
  return true;
}
bool Complete(JNIEnv* jni, jthread thread) {
  if (!jni && (!vm || vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6))) return false;
  if (jni->ExceptionCheck()) return false;
  if (jni->PushLocalFrame(16)) return Fail(jni);
  struct Locals { JNIEnv* jni; ~Locals() { jni->PopLocalFrame(nullptr); } } locals{jni};
  jmethodID method = nullptr; jlocation pc = -1;
  jobject bound = nullptr, caller = nullptr;
  if (!environment || environment->GetFrameLocation(thread, 0, &method, &pc) || method != transform || pc != 0 ||
      environment->GetLocalInstance(thread, 0, &bound) || !bound ||
      environment->GetLocalObject(thread, 0, argument_slot, &caller) || !caller) return Fail(jni);
  jobject delegate = jni->GetObjectField(bound, delegate_field);
  if (!delegate || jni->ExceptionCheck()) return Fail(jni);
  jobject receiver = jni->GetObjectField(bound, receiver_field);
  if (jni->ExceptionCheck()) return false;
  jobject range = jni->GetObjectField(bound, range_field);
  if (!range || jni->ExceptionCheck()) return Fail(jni);
  jobject type = jni->GetObjectField(delegate, type_field);
  if (!type || jni->ExceptionCheck()) return Fail(jni);
  jobject callee = jni->CallStaticObjectMethod(frame_class, create, type);
  if (!callee || jni->ExceptionCheck()) return Fail(jni);
  auto references = static_cast<jobjectArray>(jni->GetObjectField(callee, references_field));
  if (!references || jni->ExceptionCheck() || jni->GetArrayLength(references) < 1) return Fail(jni);
  // bindTo validated this capture at construction. Rechecking its runtime
  // interface here would precede the graph's filters, branches and handlers.
  // Only this captured slot uses a rooted Object[] store with ART's barrier;
  // all ordinary frame writes and argument conversions retain libcore checks.
  jni->SetObjectArrayElement(references, 0, receiver);
  if (jni->ExceptionCheck()) return false;
  jni->CallVoidMethod(caller, copy_range, callee, range, 1, 0);
  if (jni->ExceptionCheck()) return false;
  jni->CallStaticVoidMethod(transformer_class, invoke, delegate, callee);
  if (jni->ExceptionCheck()) return false;
  jni->CallVoidMethod(callee, copy_return, caller);
  return !jni->ExceptionCheck();
}
}

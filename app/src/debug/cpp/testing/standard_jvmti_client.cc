#include <jni.h>
#include "jvmti.h"
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// A user agent as an app would write it: its own environment, the
// can_redefine_classes capability and the standard RedefineClasses call.
namespace {
jvmtiEnv* client_env = nullptr;
jclass method_class = nullptr;
jmethodID method_ids[4]{};
jclass interface_static_class = nullptr;
std::vector<jmethodID> interface_static_methods;
std::atomic<jlong> single_step_count{0};

void JNICALL SingleStep(jvmtiEnv*, JNIEnv*, jthread, jmethodID, jlocation) {
  single_step_count.fetch_add(1, std::memory_order_relaxed);
}

// One class whose ClassFileLoadHook replaces the definition, as a
// transforming agent would. Guarded because ART may call the hook on any thread.
std::mutex replacement_mutex;
std::string replacement_name;
std::vector<unsigned char> replacement_bytes;
std::atomic<jlong> replacement_count{0};
std::string load_target;
jobject load_loader = nullptr;
jobjectArray load_names = nullptr;
jsize load_index = 0;
jclass dispatch_classes[2]{};
jmethodID dispatch_methods[2]{};

void JNICALL ClassFileLoadHook(jvmtiEnv* env, JNIEnv* jni, jclass, jobject, const char* name, jobject,
                               jint, const unsigned char*, jint* new_length,
                               unsigned char** new_data) {
  jobject loader = nullptr;
  jstring class_name = nullptr;
  {
    std::lock_guard<std::mutex> lock(replacement_mutex);
    if (!name) return;
    if (replacement_name == name && !replacement_bytes.empty()) {
      unsigned char* copy = nullptr;
      if (env->Allocate(static_cast<jlong>(replacement_bytes.size()), &copy) != JVMTI_ERROR_NONE) return;
      std::memcpy(copy, replacement_bytes.data(), replacement_bytes.size());
      *new_length = static_cast<jint>(replacement_bytes.size());
      *new_data = copy;
      replacement_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (load_target == name && load_names && load_index < jni->GetArrayLength(load_names)) {
      loader = jni->NewLocalRef(load_loader);
      class_name = static_cast<jstring>(jni->GetObjectArrayElement(load_names, load_index++));
    }
  }
  // Loading can recursively invoke this callback: never keep the native test
  // mutex across Java class loading. Locals protect the configured globals.
  if (loader && class_name && !jni->ExceptionCheck()) {
    jclass type = jni->FindClass("java/lang/Class");
    jmethodID for_name = type ? jni->GetStaticMethodID(type, "forName",
        "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
    if (for_name) {
      jobject loaded = jni->CallStaticObjectMethod(type, for_name, class_name, JNI_FALSE, loader);
      if (loaded) jni->DeleteLocalRef(loaded);
    }
    if (type) jni->DeleteLocalRef(type);
  }
  if (class_name) jni->DeleteLocalRef(class_name);
  if (loader) jni->DeleteLocalRef(loader);
}

jint InvokeMethodV(JNIEnv* jni, jobject receiver, jmethodID id, jint method, ...) {
  va_list arguments;
  va_start(arguments, method);
  jint value;
  if (method == 1) value = jni->CallStaticIntMethodV(method_class, id, arguments);
  else if (method == 0) value = jni->CallNonvirtualIntMethodV(receiver, method_class, id, arguments);
  else value = jni->CallIntMethodV(receiver, id, arguments);
  va_end(arguments);
  return value;
}

jint InvokeMethod(JNIEnv* jni, jobject receiver, jmethodID id, jint method, jint form) {
  if (form == 1) {
    jvalue argument{};
    argument.i = 4;
    const jvalue* arguments = method == 3 ? &argument : nullptr;
    if (method == 1) return jni->CallStaticIntMethodA(method_class, id, arguments);
    if (method == 0) return jni->CallNonvirtualIntMethodA(receiver, method_class, id, arguments);
    return jni->CallIntMethodA(receiver, id, arguments);
  }
  if (form == 2) {
    if (method == 3) return InvokeMethodV(jni, receiver, id, method, 4);
    return InvokeMethodV(jni, receiver, id, method);
  }
  // C++ JNIEnv varargs wrappers route straight to V. Use the table entry here
  // so this form actually covers the ordinary varargs entry point as well.
  if (method == 1) return jni->functions->CallStaticIntMethod(jni, method_class, id);
  if (method == 3) return jni->functions->CallIntMethod(jni, receiver, id, 4);
  if (method == 0) return jni->functions->CallNonvirtualIntMethod(jni, receiver, method_class, id);
  return jni->functions->CallIntMethod(jni, receiver, id);
}

jint InvokeCached(JNIEnv* jni, jobject receiver, jint method, jint form) {
  if (method < 0 || method >= 4 || !method_class || !method_ids[method]) {
    jclass error = jni->FindClass("java/lang/IllegalStateException");
    if (error) jni->ThrowNew(error, "Method IDs were not cached");
    return 0;
  }
  return InvokeMethod(jni, receiver, method_ids[method], method, form);
}

jint InvokeReflected(JNIEnv* jni, jobject reflected, jobject receiver, jint method, jint form) {
  jmethodID id = jni->FromReflectedMethod(reflected);
  if (jni->ExceptionCheck()) return 0;
  return InvokeMethod(jni, receiver, id, method, form);
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  if (client_env) return JNI_OK;
  if (vm->GetEnv(reinterpret_cast<void**>(&client_env), JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = 1;
  requested.can_generate_single_step_events = 1;
  // ART calls ClassFileLoadHook for redefinitions only in environments that can retransform.
  requested.can_retransform_classes = 1;
  if (client_env->AddCapabilities(&requested) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmtiEventCallbacks callbacks{};
  callbacks.SingleStep = SingleStep;
  callbacks.ClassFileLoadHook = ClassFileLoadHook;
  return client_env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE
      ? JNI_OK : JNI_ERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_setThreadSingleStep(
    JNIEnv* jni, jclass, jboolean enabled) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  jthread current = nullptr;
  jvmtiError error = client_env->GetCurrentThread(&current);
  if (error != JVMTI_ERROR_NONE) return error;
  // The explicit current jthread selects ART's per-thread interpreter mode.
  // A null thread would select global event mode and would not test this path.
  error = client_env->SetEventNotificationMode(enabled ? JVMTI_ENABLE : JVMTI_DISABLE,
                                               JVMTI_EVENT_SINGLE_STEP, current);
  jni->DeleteLocalRef(current);
  return error;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_singleStepCount(JNIEnv*, jclass) {
  return single_step_count.load(std::memory_order_relaxed);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_setClassFileReplacement(
    JNIEnv* jni, jclass, jstring name, jbyteArray bytes) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  bool enabled;
  {
    std::lock_guard<std::mutex> lock(replacement_mutex);
    replacement_name.clear();
    replacement_bytes.clear();
    if (name && bytes) {
      const char* chars = jni->GetStringUTFChars(name, nullptr);
      if (!chars) return JVMTI_ERROR_OUT_OF_MEMORY;
      replacement_name = chars;
      jni->ReleaseStringUTFChars(name, chars);
      replacement_bytes.resize(jni->GetArrayLength(bytes));
      jni->GetByteArrayRegion(bytes, 0, static_cast<jsize>(replacement_bytes.size()),
                              reinterpret_cast<jbyte*>(replacement_bytes.data()));
    }
    enabled = !replacement_bytes.empty() || load_names;
  }
  return client_env->SetEventNotificationMode(enabled ? JVMTI_ENABLE : JVMTI_DISABLE,
                                              JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_setClassFileLoads(
    JNIEnv* jni, jclass, jstring target, jobject loader, jobjectArray names) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  bool enabled;
  {
    std::lock_guard<std::mutex> lock(replacement_mutex);
    if (load_loader) jni->DeleteGlobalRef(load_loader);
    if (load_names) jni->DeleteGlobalRef(load_names);
    load_loader = nullptr; load_names = nullptr; load_target.clear(); load_index = 0;
    if (target && loader && names) {
      const char* chars = jni->GetStringUTFChars(target, nullptr);
      if (!chars) return JVMTI_ERROR_OUT_OF_MEMORY;
      load_target = chars;
      jni->ReleaseStringUTFChars(target, chars);
      load_loader = jni->NewGlobalRef(loader);
      load_names = static_cast<jobjectArray>(jni->NewGlobalRef(names));
      if (!load_loader || !load_names) return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    enabled = !replacement_bytes.empty() || load_names;
  }
  return client_env->SetEventNotificationMode(enabled ? JVMTI_ENABLE : JVMTI_DISABLE,
                                              JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheInterfaceDispatch(
    JNIEnv* jni, jclass, jint slot, jobject reflected) {
  if (slot < 0 || slot >= 2 || !client_env) return;
  jmethodID method = jni->FromReflectedMethod(reflected);
  jclass owner = nullptr;
  if (!method || client_env->GetMethodDeclaringClass(method, &owner) != JVMTI_ERROR_NONE) return;
  jclass root = static_cast<jclass>(jni->NewGlobalRef(owner));
  jni->DeleteLocalRef(owner);
  if (!root) return;
  if (dispatch_classes[slot]) jni->DeleteGlobalRef(dispatch_classes[slot]);
  dispatch_classes[slot] = root; dispatch_methods[slot] = method;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callInterfaceDispatchInt(
    JNIEnv* jni, jclass, jobject receiver, jint slot) {
  if (slot < 0 || slot >= 2 || !dispatch_methods[slot]) return -1;
  return jni->CallIntMethod(receiver, dispatch_methods[slot]);
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callInterfaceDispatchString(
    JNIEnv* jni, jclass, jobject receiver, jint slot) {
  if (slot < 0 || slot >= 2 || !dispatch_methods[slot]) return nullptr;
  return static_cast<jstring>(jni->CallObjectMethod(receiver, dispatch_methods[slot]));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_classFileReplacements(JNIEnv*, jclass) {
  return replacement_count.load(std::memory_order_relaxed);
}

namespace {
void DeallocateExtension(jvmtiEnv* env, jvmtiExtensionFunctionInfo* info) {
  auto release = [env](void* memory) { env->Deallocate(static_cast<unsigned char*>(memory)); };
  release(info->id);
  release(info->short_description);
  for (jint i = 0; i < info->param_count; ++i) release(info->params[i].name);
  release(info->params);
  release(info->errors);
}

// ART's standard extension, as a hot-reload tool would find it.
jvmtiExtensionFunction FindExtension(const char* id) {
  jint count = 0;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (client_env->GetExtensionFunctions(&count, &infos) != JVMTI_ERROR_NONE) return nullptr;
  jvmtiExtensionFunction found = nullptr;
  for (jint i = 0; i < count; ++i) {
    if (std::strcmp(infos[i].id, id) == 0) found = infos[i].func;
    DeallocateExtension(client_env, &infos[i]);
  }
  client_env->Deallocate(reinterpret_cast<unsigned char*>(infos));
  return found;
}
}  // namespace

// Append a DEX, jar or APK file to a BaseDexClassLoader's path, after its
// existing entries, through com.android.art.classloader.add_to_dex_class_loader.
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_addToClassLoader(
    JNIEnv* jni, jclass, jobject loader, jstring path) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!loader || !path) return JVMTI_ERROR_NULL_POINTER;
  static const jvmtiExtensionFunction add =
      FindExtension("com.android.art.classloader.add_to_dex_class_loader");
  if (!add) return JVMTI_ERROR_NOT_AVAILABLE;
  const char* segment = jni->GetStringUTFChars(path, nullptr);
  if (!segment) return JVMTI_ERROR_OUT_OF_MEMORY;
  const jvmtiError result = add(client_env, loader, segment);
  jni->ReleaseStringUTFChars(path, segment);
  return result;
}

namespace {
jint Redefine(JNIEnv* jni, jobjectArray classes, jobjectArray dexes, bool structural) {
  if (!client_env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!classes || !dexes) return JVMTI_ERROR_NULL_POINTER;
  const jint count = jni->GetArrayLength(classes);
  if (count != jni->GetArrayLength(dexes)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  std::vector<jvmtiClassDefinition> batch;
  std::vector<std::vector<unsigned char>> buffers(count);
  for (jint i = 0; i < count; ++i) {
    auto klass = static_cast<jclass>(jni->GetObjectArrayElement(classes, i));
    auto bytes = static_cast<jbyteArray>(jni->GetObjectArrayElement(dexes, i));
    if (!klass || !bytes) return JVMTI_ERROR_NULL_POINTER;
    const jint size = jni->GetArrayLength(bytes);
    buffers[i].resize(size);
    jni->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(buffers[i].data()));
    jni->DeleteLocalRef(bytes);
    batch.push_back({klass, size, buffers[i].data()});
  }
  if (!structural) return client_env->RedefineClasses(count, batch.data());
  static const jvmtiExtensionFunction redefine =
      FindExtension("com.android.art.class.structurally_redefine_classes");
  return redefine ? redefine(client_env, count, batch.data()) : JVMTI_ERROR_NOT_AVAILABLE;
}
}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_redefine(
    JNIEnv* jni, jclass, jobjectArray classes, jobjectArray dexes) {
  return Redefine(jni, classes, dexes, false);
}

// ART's own structural extension, bypassing the standard entry the engine routes.
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_redefineStructurally(
    JNIEnv* jni, jclass, jobjectArray classes, jobjectArray dexes) {
  return Redefine(jni, classes, dexes, true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheInterfaceStaticMethods(
    JNIEnv* jni, jclass, jclass type, jobjectArray methods) {
  if (interface_static_class) jni->DeleteGlobalRef(interface_static_class);
  interface_static_class = static_cast<jclass>(jni->NewGlobalRef(type));
  interface_static_methods.clear();
  for (jsize i = 0; i < jni->GetArrayLength(methods); ++i) {
    jobject method = jni->GetObjectArrayElement(methods, i);
    jmethodID id = jni->FromReflectedMethod(method);
    jni->DeleteLocalRef(method);
    if (jni->ExceptionCheck()) return;
    interface_static_methods.push_back(id);
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callInterfaceStatic(
    JNIEnv* jni, jclass, jobject receiver, jint method) {
  if (method < 0 || static_cast<size_t>(method) >= interface_static_methods.size()) {
    jclass error = jni->FindClass("java/lang/IllegalStateException");
    if (error) jni->ThrowNew(error, "Interface method ID was not cached");
    return 0;
  }
  return jni->functions->CallStaticIntMethod(
      jni, interface_static_class, interface_static_methods[method], receiver);
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheMethodIds(
    JNIEnv* jni, jclass, jclass type) {
  if (method_class) jni->DeleteGlobalRef(method_class);
  method_class = static_cast<jclass>(jni->NewGlobalRef(type));
  method_ids[0] = jni->GetMethodID(type, "directValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[1] = jni->GetStaticMethodID(type, "staticValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[2] = jni->GetMethodID(type, "virtualValue", "()I");
  if (jni->ExceptionCheck()) return;
  method_ids[3] = jni->GetMethodID(type, "signatureValue", "(I)I");
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callCachedMethod(
    JNIEnv* jni, jclass, jobject receiver, jint method) {
  return InvokeCached(jni, receiver, method, 0);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callReflectedMethod(
    JNIEnv* jni, jclass, jobject reflected, jobject receiver, jint method) {
  return InvokeReflected(jni, reflected, receiver, method, 0);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callCachedMethodVariant(
    JNIEnv* jni, jclass, jobject receiver, jint method, jint form) {
  return InvokeCached(jni, receiver, method, form);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_callReflectedMethodVariant(
    JNIEnv* jni, jclass, jobject reflected, jobject receiver, jint method, jint form) {
  return InvokeReflected(jni, reflected, receiver, method, form);
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_lookupMethodId(
    JNIEnv* jni, jclass, jclass type, jint method) {
  const char* names[] = {"directValue", "staticValue", "virtualValue", "signatureValue"};
  if (method == 1) {
    (void)jni->GetStaticMethodID(type, names[method], "()I");
  } else {
    (void)jni->GetMethodID(type, names[method], method == 3 ? "(I)I" : "()I");
  }
}

// Field IDs resolved before a redefinition and used unchanged afterwards:
// 0 = int i, 1 = long j, 2 = Object o, 3 = static int si, 4 = static Object so.
namespace {
jclass field_class = nullptr;
jfieldID field_ids[5]{};

bool StaticField(jint field) { return field >= 3; }

jlong ReadField(JNIEnv* jni, jobject receiver, jfieldID id, jint field) {
  switch (field) {
    case 0: return jni->GetIntField(receiver, id);
    case 1: return jni->GetLongField(receiver, id);
    case 2: { jobject value = jni->GetObjectField(receiver, id); jlong present = value != nullptr;
              if (value) jni->DeleteLocalRef(value); return present; }
    case 3: return jni->GetStaticIntField(field_class, id);
    default: { jobject value = jni->GetStaticObjectField(field_class, id); jlong present = value != nullptr;
               if (value) jni->DeleteLocalRef(value); return present; }
  }
}

void WriteField(JNIEnv* jni, jobject receiver, jfieldID id, jint field, jlong value) {
  switch (field) {
    case 0: jni->SetIntField(receiver, id, static_cast<jint>(value)); break;
    case 1: jni->SetLongField(receiver, id, value); break;
    case 2: jni->SetObjectField(receiver, id, nullptr); break;
    case 3: jni->SetStaticIntField(field_class, id, static_cast<jint>(value)); break;
    default: jni->SetStaticObjectField(field_class, id, nullptr); break;
  }
}

bool CachedField(JNIEnv* jni, jint field) {
  if (field >= 0 && field < 5 && field_class && field_ids[field]) return true;
  jclass error = jni->FindClass("java/lang/IllegalStateException");
  if (error) jni->ThrowNew(error, "Field IDs were not cached");
  return false;
}
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheFieldIds(JNIEnv* jni, jclass,
                                                                         jclass type) {
  if (field_class) jni->DeleteGlobalRef(field_class);
  field_class = static_cast<jclass>(jni->NewGlobalRef(type));
  const char* names[] = {"i", "j", "o", "si", "so"};
  const char* types[] = {"I", "J", "Ljava/lang/Object;", "I", "Ljava/lang/Object;"};
  for (jint field = 0; field < 5; ++field) {
    field_ids[field] = StaticField(field) ? jni->GetStaticFieldID(type, names[field], types[field])
                                          : jni->GetFieldID(type, names[field], types[field]);
    if (!field_ids[field]) return;
  }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_readCachedField(JNIEnv* jni, jclass,
                                                                           jobject receiver,
                                                                           jint field) {
  return CachedField(jni, field) ? ReadField(jni, receiver, field_ids[field], field) : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_writeCachedField(JNIEnv* jni, jclass,
                                                                            jobject receiver,
                                                                            jint field,
                                                                            jlong value) {
  if (CachedField(jni, field)) WriteField(jni, receiver, field_ids[field], field, value);
}

// A JNI ID obtained now from a Field object reflected before the redefinition.
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_readReflectedField(JNIEnv* jni, jclass,
                                                                              jobject reflected,
                                                                              jobject receiver,
                                                                              jint field) {
  if (!CachedField(jni, field)) return 0;
  jfieldID id = jni->FromReflectedField(reflected);
  if (jni->ExceptionCheck() || !id) return 0;
  return ReadField(jni, receiver, id, field);
}

// Whether any class table holds a class with this JVMTI signature; unlike
// ClassLoader.findLoadedClass on ART, this never defines the class.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_isLoaded(JNIEnv* jni, jclass,
                                                                     jstring signature) {
  if (!client_env || !signature) return JNI_FALSE;
  const char* wanted = jni->GetStringUTFChars(signature, nullptr);
  if (!wanted) return JNI_FALSE;
  jint count = 0;
  jclass* classes = nullptr;
  bool found = false;
  if (client_env->GetLoadedClasses(&count, &classes) == JVMTI_ERROR_NONE) {
    for (jint i = 0; i < count; ++i) {
      char* name = nullptr;
      if (!found && client_env->GetClassSignature(classes[i], &name, nullptr) == JVMTI_ERROR_NONE) {
        found = std::strcmp(name, wanted) == 0;
        client_env->Deallocate(reinterpret_cast<unsigned char*>(name));
      }
      jni->DeleteLocalRef(classes[i]);
    }
    client_env->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  jni->ReleaseStringUTFChars(signature, wanted);
  return found ? JNI_TRUE : JNI_FALSE;
}

namespace {
jclass constructor_class = nullptr;
jmethodID constructor_id = nullptr;
}

// Resolve a constructor ID before redefinition; NewObject later reuses it.
extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_cacheConstructor(JNIEnv* jni, jclass,
                                                                             jclass type,
                                                                             jstring signature) {
  const char* descriptor = jni->GetStringUTFChars(signature, nullptr);
  if (!descriptor) return;
  if (!constructor_class) constructor_class = static_cast<jclass>(jni->NewGlobalRef(type));
  constructor_id = jni->GetMethodID(type, "<init>", descriptor);
  jni->ReleaseStringUTFChars(signature, descriptor);
}

// The table's varargs NewObject with the cached (int) constructor ID.
extern "C" JNIEXPORT jobject JNICALL
Java_com_github_ytlog_artdcevm_testing_StandardJvmtiClient_newObjectCached(JNIEnv* jni, jclass,
                                                                            jint value) {
  if (!constructor_class || !constructor_id) {
    jclass error = jni->FindClass("java/lang/IllegalStateException");
    if (error) jni->ThrowNew(error, "Constructor ID was not cached");
    return nullptr;
  }
  return jni->functions->NewObject(jni, constructor_class, constructor_id, value);
}

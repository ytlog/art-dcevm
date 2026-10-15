#include "platform/common/interface_snapshot.h"
#include <limits>

namespace artdcevm::interface_redefinition {
namespace {
class LocalFrame {
 public:
  explicit LocalFrame(JNIEnv* jni) : jni_(jni), valid(jni->PushLocalFrame(32) == JNI_OK) {}
  ~LocalFrame() { if (valid) jni_->PopLocalFrame(nullptr); }
 private:
  JNIEnv* jni_;
 public:
  bool valid;
};
}

jvmtiError SnapshotTypes(JNIEnv* jni, jvmtiEnv* env, jclass target, jint count,
                         const jclass* loaded, TypeSnapshot* output, std::string* error) {
  const auto fail = [&](jvmtiError code, const char* reason) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = reason;
    return code;
  };
  LocalFrame frame(jni);
  if (!frame.valid) return fail(JVMTI_ERROR_OUT_OF_MEMORY, "interface snapshot local frame allocation failed");
  // IdentityHashMap keeps distinct same-named classes in different loaders
  // separate, including identity-hash collisions, without an all-pairs scan.
  jclass map_class = jni->FindClass("java/util/IdentityHashMap");
  if (!map_class) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot identity map unavailable");
  jmethodID construct = jni->GetMethodID(map_class, "<init>", "()V");
  if (!construct) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot identity map constructor unavailable");
  jmethodID put = jni->GetMethodID(map_class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  if (!put) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot identity map put unavailable");
  jmethodID get = jni->GetMethodID(map_class, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
  if (!get) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot identity map get unavailable");
  jclass integer = jni->FindClass("java/lang/Integer");
  if (!integer) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot Integer unavailable");
  jmethodID box = jni->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;");
  if (!box) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot boxing unavailable");
  jmethodID unbox = jni->GetMethodID(integer, "intValue", "()I");
  if (!unbox) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot unboxing unavailable");
  jclass proxy = jni->FindClass("java/lang/reflect/Proxy");
  if (!proxy) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot Proxy unavailable");
  jmethodID is_proxy = jni->GetStaticMethodID(proxy, "isProxyClass", "(Ljava/lang/Class;)Z");
  if (!is_proxy) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot Proxy check unavailable");
  jobject index = jni->NewObject(map_class, construct);
  if (!index) return fail(JVMTI_ERROR_OUT_OF_MEMORY, "interface snapshot identity map allocation failed");

  std::vector<jclass> types;
  for (jint i = 0; i < count; ++i) {
    jclass candidate = loaded[i];
    jint status = 0;
    jvmtiError result = env->GetClassStatus(candidate, &status);
    if (result != JVMTI_ERROR_NONE) return fail(result, "interface snapshot class status unavailable");
    if (!(status & JVMTI_CLASS_STATUS_PREPARED) || (status & JVMTI_CLASS_STATUS_ERROR)) continue;
    const bool selected = jni->IsAssignableFrom(candidate, target);
    if (jni->ExceptionCheck()) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot assignability failed");
    if (!selected) continue;
    const bool generated_proxy = jni->CallStaticBooleanMethod(proxy, is_proxy, candidate);
    if (jni->ExceptionCheck()) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot Proxy check failed");
    if (generated_proxy) return fail(JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface redefinition rejects Proxy implementations");
    types.push_back(candidate);
  }
  if (types.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    return fail(JVMTI_ERROR_OUT_OF_MEMORY, "interface snapshot has too many classes");
  for (size_t i = 0; i < types.size(); ++i) {
    jobject value = jni->CallStaticObjectMethod(integer, box, static_cast<jint>(i));
    if (!value) return fail(JVMTI_ERROR_OUT_OF_MEMORY, "interface snapshot index allocation failed");
    jobject previous = jni->CallObjectMethod(index, put, types[i], value);
    jni->DeleteLocalRef(value);
    if (previous) jni->DeleteLocalRef(previous);
    if (jni->ExceptionCheck()) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot index insertion failed");
  }
  const auto lookup = [&](jclass klass, int32_t* position) {
    *position = -1;
    if (!klass) return true;
    jobject value = jni->CallObjectMethod(index, get, klass);
    if (jni->ExceptionCheck()) return false;
    if (value) {
      *position = jni->CallIntMethod(value, unbox);
      jni->DeleteLocalRef(value);
    }
    return !jni->ExceptionCheck();
  };
  std::vector<std::vector<size_t>> children(types.size());
  std::vector<size_t> incoming(types.size());
  std::vector<int32_t> superclass(types.size(), -1);
  for (size_t i = 0; i < types.size(); ++i) {
    jclass parent = jni->GetSuperclass(types[i]);
    if (jni->ExceptionCheck()) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot superclass lookup failed");
    const bool found = lookup(parent, &superclass[i]);
    if (parent) jni->DeleteLocalRef(parent);
    if (!found) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot superclass index failed");
    if (superclass[i] >= 0) {
      children[superclass[i]].push_back(i);
      ++incoming[i];
    }
    jint interface_count = 0;
    struct Interfaces {
      jvmtiEnv* env;
      jclass* value = nullptr;
      ~Interfaces() { if (value) env->Deallocate(reinterpret_cast<unsigned char*>(value)); }
    } interfaces{env};
    jvmtiError result = env->GetImplementedInterfaces(types[i], &interface_count, &interfaces.value);
    if (result != JVMTI_ERROR_NONE) return fail(result, "interface snapshot parent interfaces unavailable");
    bool valid = true;
    for (jint j = 0; j < interface_count; ++j) {
      int32_t position;
      if (valid) {
        valid = lookup(interfaces.value[j], &position);
        if (valid && position >= 0) {
          children[position].push_back(i);
          ++incoming[i];
        }
      }
      jni->DeleteLocalRef(interfaces.value[j]);
    }
    if (!valid) return fail(JVMTI_ERROR_INTERNAL, "interface snapshot parent interface index failed");
  }
  // Kahn traversal over direct edges: each affected superclass and interface
  // is processed before its descendants. No pairwise assignability ordering.
  std::vector<size_t> order;
  for (size_t i = 0; i < types.size(); ++i)
    if (incoming[i] == 0) order.push_back(i);
  for (size_t head = 0; head < order.size(); ++head)
    for (size_t child : children[order[head]])
      if (--incoming[child] == 0) order.push_back(child);
  if (order.size() != types.size())
    return fail(JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION, "interface snapshot hierarchy is cyclic");
  if (order.empty() || !jni->IsSameObject(types[order.front()], target))
    return fail(JVMTI_ERROR_INTERNAL, "interface snapshot target is missing or not first");
  TypeSnapshot next;
  std::vector<int32_t> positions(types.size());
  for (size_t i = 0; i < order.size(); ++i) positions[order[i]] = static_cast<int32_t>(i);
  for (size_t original : order) {
    next.types.push_back(types[original]);
    next.superclass.push_back(superclass[original] < 0 ? -1 : positions[superclass[original]]);
  }
  *output = std::move(next);
  return JVMTI_ERROR_NONE;
}
}  // namespace artdcevm::interface_redefinition

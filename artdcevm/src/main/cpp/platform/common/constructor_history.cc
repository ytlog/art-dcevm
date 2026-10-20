#include "platform/common/constructor_history.h"

#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

#include "platform/common/member_retirement.h"
#include "evolution/class_schema.h"
#include "platform/symbols/constructor_history_symbols.h"
#include "platform/symbols/method_handle_lookup_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"

namespace artdcevm::constructor_history {
namespace {
enum class Accessor { kEmpty, kDirect, kInstantiationError };
struct Root {
  std::atomic<Accessor> accessor{Accessor::kEmpty};
  std::atomic<uint64_t> public_epoch{0};
};
enum class TagKind { kOwner, kCopy };
struct TagValue { TagKind kind; };
struct Owner : TagValue {
  Owner() : TagValue{TagKind::kOwner} {}
  std::map<std::pair<jlong, bool>, std::shared_ptr<Root>> roots;
};
struct Copy : TagValue {
  explicit Copy(std::shared_ptr<Root> value) : TagValue{TagKind::kCopy}, root(std::move(value)) {}
  const std::shared_ptr<Root> root;
};
struct State {
  JavaVM* vm = nullptr;
  jvmtiEnv* env = nullptr;
  jrawMonitorID monitor = nullptr;
  std::atomic<bool> ready{false};
  std::atomic<uint64_t> epoch{1};
  jfieldID target = nullptr, declaring = nullptr, flags = nullptr, access = nullptr, override_field = nullptr;
  jmethodID public_lookup = nullptr, declared_lookup = nullptr, parameters = nullptr;
  std::array<jclass, 8> primitive{}, boxed{};
  jclass class_type = nullptr;
  jclass instantiation = nullptr, missing = nullptr, invocation = nullptr, illegal_argument = nullptr;
  jclass cast_error = nullptr, null_error = nullptr, initializer_error = nullptr, linkage_error = nullptr;
  jclass access_error = nullptr, internal_error = nullptr, out_of_memory = nullptr;
  jmethodID invocation_init = nullptr, argument_init = nullptr;
  void* one_target = nullptr; const char* one_name = nullptr;
  void* many_target = nullptr; const char* many_name = nullptr;
  jobject (*one)(JNIEnv*, jobject, jobjectArray) = nullptr;
  jobjectArray (*many)(JNIEnv*, jobject, jboolean) = nullptr;
  jboolean (*same_object)(JNIEnv*, jobject, jobject) = nullptr;
  void* (*current_thread)() = nullptr;
  void* (*calling_class)(void*, size_t) = nullptr;
  bool (*verify_access)(void*, void*, uint32_t, void*) = nullptr;
  void* (*decode_global)(JavaVM*, jobject) = nullptr;
};
State* state = nullptr;
// ART owns weak-reference movement and clearing. Each attachment keeps one
// weak identity and native accessor state; neither roots an application loader.
struct Recent {
  jweak constructor = nullptr;
  std::shared_ptr<Root> root;
};
thread_local Recent recent;
// The nontrivial owner still registers its native destructor on first use.
// Cache only its stable TLS address so subsequent hits resolve one trivial TLS
// slot rather than both the initialization guard and the owner. This is not a
// Java identity cache: every hit still checks ART's weak reference below.
thread_local Recent* recent_address = nullptr;
Recent& Cached() {
  if (!recent_address) recent_address = &recent;
  return *recent_address;
}
void Ended(JNIEnv* jni) {
  if (recent.constructor) jni->DeleteWeakGlobalRef(recent.constructor);
  recent = {};
  recent_address = nullptr;
}
struct Locals {
  JNIEnv* jni;
  bool active = true;
  ~Locals() { if (active) jni->PopLocalFrame(nullptr); }
  jobject Return(jobject result) { active = false; return jni->PopLocalFrame(result); }
};
struct Lock {
  bool held = state->env->RawMonitorEnter(state->monitor) == JVMTI_ERROR_NONE;
  ~Lock() { if (held) state->env->RawMonitorExit(state->monitor); }
};
// The tag is agent-owned native storage, not an ART address. Public JNI roots
// keep the tagged object alive while a hook reads its immutable Copy. ObjectFree
// therefore needs no lock and cannot deadlock a collector behind a VM call.
void JNICALL Freed(jvmtiEnv*, jlong tag) {
  auto* value = reinterpret_cast<TagValue*>(static_cast<uintptr_t>(tag));
  if (value->kind == TagKind::kOwner) delete static_cast<Owner*>(value);
  else delete static_cast<Copy*>(value);
}
bool Failure(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(state->internal_error, "constructor history unavailable");
  return false;
}
template <class T> bool Tagged(jobject object, T** value) {
  jlong tag = 0;
  if (state->env->GetTag(object, &tag)) return false;
  *value = reinterpret_cast<T*>(static_cast<uintptr_t>(tag));
  return true;
}
template <class T> T* Publish(jobject object, std::unique_ptr<T> value) {
  if (state->env->SetTag(object, reinterpret_cast<uintptr_t>(value.get()))) return nullptr;
  return value.release();
}
// Class.getConstructor and getDeclaredConstructor share ART's native entry,
// while JDK keeps separate public and declared reflection-cache roots. Identify
// the actual public caller by opaque, bootstrap-resolved IDs, never DEX slots.
bool PublicLookup(JNIEnv* jni, bool* public_only) {
  void* self = state->current_thread();
  void* class_type = state->decode_global(state->vm, state->class_type);
  if (!self || !class_type) return false;
  jthread thread = nullptr;
  if (state->env->GetCurrentThread(&thread)) return false;
  bool found = false;
  for (jint depth = 0; depth < 8; ++depth) {
    // Do not request IDs for application frames: pointer-ID mode would then
    // mark the calling class as structurally unmodifiable. The named walker
    // first proves this is still a bootstrap Class frame, without encoding IDs.
    if (state->calling_class(self, depth) != class_type) break;
    jvmtiFrameInfo frame{};
    jint count = 0;
    if (state->env->GetStackTrace(thread, depth, 1, &frame, &count) || count != 1) break;
    if (frame.method == state->public_lookup) { *public_only = true; found = true; break; }
    if (frame.method == state->declared_lookup) { *public_only = false; found = true; break; }
  }
  jni->DeleteLocalRef(thread);
  return found;
}

bool Capture(JNIEnv* jni, jobject constructor, bool public_only) {
  if (!constructor) return true;
  jobject owner = jni->GetObjectField(constructor, state->declaring);
  if (!owner) return Failure(jni);
  jobject loader = nullptr;
  const jvmtiError result = state->env->GetClassLoader(static_cast<jclass>(owner), &loader);
  if (result) { jni->DeleteLocalRef(owner); return Failure(jni); }
  if (!loader) { jni->DeleteLocalRef(owner); return true; }  // Bootstrap classes do not evolve.
  jni->DeleteLocalRef(loader);
  const jlong method = jni->GetLongField(constructor, state->target);
  bool passed = false, allocation_failed = false;
  try {
    Lock lock;
    Owner* owner_state = nullptr;
    if (lock.held && Tagged(owner, &owner_state)) {
      if (!owner_state) owner_state = Publish(owner, std::make_unique<Owner>());
      if (owner_state) {
        auto& root = owner_state->roots[{method, public_only}];
        if (!root) root = std::make_shared<Root>();
        Copy* copy = nullptr;
        if (Tagged(constructor, &copy)) passed = copy || Publish(constructor, std::make_unique<Copy>(root));
      }
    }
  } catch (const std::bad_alloc&) { allocation_failed = true; }
  jni->DeleteLocalRef(owner);
  if (allocation_failed) {
    if (!jni->ExceptionCheck()) jni->ThrowNew(state->out_of_memory, "constructor history allocation failed");
    return false;
  }
  return passed || Failure(jni);
}
jobject One(JNIEnv* jni, jobject klass, jobjectArray args) noexcept {
  jobject constructor = state->one(jni, klass, args);
  if (!constructor || jni->ExceptionCheck() || !state->ready.load(std::memory_order_acquire)) return constructor;
  bool public_only = false;
  // This internal entry is also callable directly through JNI; it has no JDK
  // lookup-root counterpart. Leave that non-public discovery path alone.
  if (!PublicLookup(jni, &public_only)) return constructor;
  return Capture(jni, constructor, public_only) ? constructor : nullptr;
}
jobjectArray Many(JNIEnv* jni, jobject klass, jboolean public_only) noexcept {
  jobjectArray constructors = state->many(jni, klass, public_only);
  if (!constructors || jni->ExceptionCheck() || !state->ready.load(std::memory_order_acquire)) return constructors;
  const jsize count = jni->GetArrayLength(constructors);
  for (jsize i = 0; i < count; ++i) {
    jobject constructor = jni->GetObjectArrayElement(constructors, i);
    const bool passed = Capture(jni, constructor, public_only);
    if (constructor) jni->DeleteLocalRef(constructor);
    if (!passed) return nullptr;
  }
  return constructors;
}
struct Found { Root* root; bool failed; };
__attribute__((noinline)) Found FindUncached(JNIEnv* jni, jobject constructor, Recent& cached) {
  jlong tag = 0;
  if (state->env->GetTag(constructor, &tag)) { Failure(jni); return {nullptr, true}; }
  auto* copy = reinterpret_cast<Copy*>(static_cast<uintptr_t>(tag));
  jweak weak = jni->NewWeakGlobalRef(constructor);
  if (!weak) return {nullptr, true};
  if (cached.constructor) jni->DeleteWeakGlobalRef(cached.constructor);
  cached.constructor = weak;
  cached.root = copy ? copy->root : std::shared_ptr<Root>{};
  return {copy ? copy->root.get() : nullptr, false};
}
Found Find(JNIEnv* jni, jobject constructor) {
  Recent& cached = Cached();
  if (cached.constructor && (state->same_object
      ? state->same_object(jni, constructor, cached.constructor)
      : jni->IsSameObject(constructor, cached.constructor))) return {cached.root.get(), false};
  return FindUncached(jni, constructor, cached);
}

bool Accessible(JNIEnv* jni, jobject constructor, jobject owner, uint32_t class_flags) {
  const uint32_t flags = jni->GetIntField(constructor, state->access);
  if (jni->GetBooleanField(constructor, state->override_field) || ((flags & class_flags & evolution::kAccPublic) != 0)) return true;
  jobject global = jni->NewGlobalRef(owner);
  if (!global) return false;
  void* caller = state->calling_class(state->current_thread(), 2);
  void* declaring = state->decode_global(state->vm, global);
  const bool allowed = caller && declaring &&
      ((class_flags & evolution::kAccPublic) || state->verify_access(nullptr, declaring, class_flags, caller)) &&
      state->verify_access(nullptr, declaring, flags, caller);
  jni->DeleteGlobalRef(global);
  if (!allowed && !jni->ExceptionCheck()) jni->ThrowNew(state->access_error, "constructor is not accessible");
  return allowed;
}
void Throw(JNIEnv* jni, jclass type, const char* message, bool wrap = false) {
  jni->ThrowNew(type, message);
  if (!wrap || !jni->ExceptionCheck()) return;
  jthrowable cause = jni->ExceptionOccurred();
  jni->ExceptionClear();
  jobject error = jni->NewObject(state->invocation, state->invocation_init, cause);
  if (error) jni->Throw(static_cast<jthrowable>(error));
  if (cause) jni->DeleteLocalRef(cause);
  if (error) jni->DeleteLocalRef(error);
}
bool ArgumentFailure(JNIEnv* jni, jclass cause) {
  jni->ThrowNew(cause, "constructor argument conversion failed");
  jthrowable error = jni->ExceptionOccurred();
  jni->ExceptionClear();
  jobject wrapped = jni->NewObject(state->illegal_argument, state->argument_init, error);
  if (wrapped) jni->Throw(static_cast<jthrowable>(wrapped));
  if (wrapped) jni->DeleteLocalRef(wrapped);
  if (error) jni->DeleteLocalRef(error);
  return false;
}
bool Arguments(JNIEnv* jni, jobject constructor, jobjectArray args) {
  auto parameters = static_cast<jobjectArray>(jni->CallObjectMethod(constructor, state->parameters));
  if (!parameters || jni->ExceptionCheck()) return false;
  const jsize count = jni->GetArrayLength(parameters), actual = args ? jni->GetArrayLength(args) : 0;
  if (count != actual) { Throw(jni, state->illegal_argument, "wrong number of constructor arguments"); return false; }
  // boolean, byte, char, short, int, long, float, double. Each mask lists legal
  // boxed source kinds for Java method-invocation primitive widening.
  constexpr uint32_t sources[] = {1, 2, 4, 2|8, 2|4|8|16, 2|4|8|16|32,
                                 2|4|8|16|32|64, 2|4|8|16|32|64|128};
  for (jsize i = 0; i < count; ++i) {
    jobject type = jni->GetObjectArrayElement(parameters, i);
    jobject value = jni->GetObjectArrayElement(args, i);
    int primitive = -1;
    for (size_t p = 0; p < state->primitive.size(); ++p)
      if (jni->IsSameObject(type, state->primitive[p])) { primitive = static_cast<int>(p); break; }
    bool compatible = !value;
    if (primitive >= 0) {
      if (!value) return ArgumentFailure(jni, state->null_error);
      compatible = false;
      for (size_t p = 0; p < state->boxed.size(); ++p)
        if ((sources[primitive] & (1u << p)) && jni->IsInstanceOf(value, state->boxed[p])) { compatible = true; break; }
    } else if (value) compatible = jni->IsInstanceOf(value, static_cast<jclass>(type));
    jni->DeleteLocalRef(type);
    if (value) jni->DeleteLocalRef(value);
    if (!compatible) return ArgumentFailure(jni, state->cast_error);
  }
  return true;
}
// Keep metadata lookup, argument adaptation and exception construction out of
// the cache-hit frame. They require roots/registers that a proved public
// allocation does not use; the hot entry can restore its frame before calling
// ART. Both paths still use the same weak identity and transaction epoch.
__attribute__((noinline)) jobject InvokeUncached(JNIEnv* jni, jobject constructor, jobjectArray args,
                                                NewInstance original, Root* root, uint64_t epoch) {
  if (jni->PushLocalFrame(16)) return nullptr;
  Locals locals{jni};
  jobject owner = jni->GetObjectField(constructor, state->declaring);
  if (!owner) { Failure(jni); return nullptr; }
  const uint32_t flags = jni->GetIntField(owner, state->flags);
  if (!Accessible(jni, constructor, owner, flags)) return nullptr;
  Accessor accessor = root->accessor.load(std::memory_order_acquire);
  if (accessor == Accessor::kEmpty) {
    Accessor wanted = (flags & evolution::kAccAbstract) ? Accessor::kInstantiationError : Accessor::kDirect;
    root->accessor.compare_exchange_strong(accessor, wanted, std::memory_order_acq_rel);
    accessor = root->accessor.load(std::memory_order_acquire);
  }
  if (accessor == Accessor::kInstantiationError) {
    Throw(jni, state->instantiation, "declaring class was not instantiable when the accessor was acquired");
    return nullptr;
  }
  const jlong target = jni->GetLongField(constructor, state->target);
  const bool retired = member_retirement::IsRetiredMethod(reinterpret_cast<const void*>(static_cast<uintptr_t>(target)));
  if ((flags & evolution::kAccAbstract) || retired) {
    if (!Arguments(jni, constructor, args)) return nullptr;
    Throw(jni, (flags & evolution::kAccAbstract) ? state->instantiation : state->missing,
          (flags & evolution::kAccAbstract) ? "declaring class is not instantiable" : "constructor retired by structural redefinition", true);
    return nullptr;
  }
  const bool public_member = (flags & evolution::kAccPublic) && (jni->GetIntField(constructor, state->access) & evolution::kAccPublic);
  jobject result = original(jni, constructor, args);
  if (jni->ExceptionCheck()) {
    jthrowable error = jni->ExceptionOccurred();
    jni->ExceptionClear();
    // Accessor creation precedes argument checking and the constructor body,
    // but follows access checks and class initialization. Failed initialization
    // must not warm the shared reflection root.
    if (jni->IsInstanceOf(error, state->initializer_error) || jni->IsInstanceOf(error, state->linkage_error) ||
        jni->IsInstanceOf(error, state->access_error)) root->accessor.store(Accessor::kEmpty, std::memory_order_release);
    jni->Throw(error);
    jni->DeleteLocalRef(error);
  }
  if (root->accessor.load(std::memory_order_acquire) == Accessor::kDirect && public_member)
    root->public_epoch.store(epoch, std::memory_order_release);
  return locals.Return(result);
}
}  // namespace
bool Available() { return state && state->ready.load(std::memory_order_acquire); }
uint32_t CacheProofs() { return !Available() ? 0 : state->same_object ? 3u : 1u; }
const char* CacheReason() {
  return !Available() ? "constructor tag metadata unavailable" : state->same_object ? "" : "using public JNI weak identity";
}
void Invalidate() noexcept { if (Available()) state->epoch.fetch_add(1, std::memory_order_acq_rel); }

jobject Invoke(JNIEnv* jni, jobject constructor, jobjectArray args, NewInstance original, NewInstance fallback) {
  if (!Available()) return fallback(jni, constructor, args);
  auto found = Find(jni, constructor);
  if (found.failed) return nullptr;
  Root* root = found.root;
  // Pre-initialization and non-standard constructor discovery keep the existing
  // retirement guard; no invented root is shared with a public lookup.
  if (!root) return fallback(jni, constructor, args);
  const uint64_t epoch = state->epoch.load(std::memory_order_acquire);
  // This epoch already proves public access, an instantiable owner and a live
  // target. Mapping invalidates it while ART still suspends callers, so the
  // shared retirement guard need not encode/check this target again.
  if (root->public_epoch.load(std::memory_order_acquire) == epoch)
    return original(jni, constructor, args);
  return InvokeUncached(jni, constructor, args, original, root, epoch);
}
void Committed(JNIEnv*, jint count, const jvmtiClassDefinition* definitions) {
  if (!Available()) return;
  // A global generation invalidates only the cached access/instantiability
  // verdict, once per root after a successful transaction. It contains no
  // object identity and remains correct if ART replaces a tagged Class.
  state->epoch.fetch_add(1, std::memory_order_acq_rel);
  Lock lock;
  if (!lock.held) return;
  for (jint i = 0; i < count; ++i) {
    jlong tag = 0;
    if (state->env->GetTag(definitions[i].klass, &tag) == JVMTI_ERROR_NONE && tag)
      reinterpret_cast<Owner*>(static_cast<uintptr_t>(tag))->roots.clear();
  }
}

bool Prepare(JavaVM* vm, JNIEnv* jni, std::vector<Hook>* hooks, std::string* why) {
  auto append_hooks = [&] {
    hooks->push_back({state->one_target, HookAddress(One), HookSlot(&state->one), state->one_name});
    hooks->push_back({state->many_target, HookAddress(Many), HookSlot(&state->many), state->many_name});
  };
  if (Available()) { append_hooks(); return true; }
  if (!state) state = new State;
  state->vm = vm;
  auto fail = [&](const char* message) { if (jni->ExceptionCheck()) jni->ExceptionClear(); *why = message; return false; };
  using namespace symbols::constructor_history;
  void* one = nullptr; const char* one_name = nullptr;
  void* many = nullptr; const char* many_name = nullptr;
  for (auto symbol : {kDeclaredConstructor, kDeclaredConstructorUnique}) if ((one = symbols::Find(symbol))) { one_name = symbol.name; break; }
  for (auto symbol : {kDeclaredConstructors, kDeclaredConstructorsUnique}) if ((many = symbols::Find(symbol))) { many_name = symbol.name; break; }
  state->current_thread = reinterpret_cast<decltype(state->current_thread)>(symbols::Find(symbols::runtime_thread::kCurrentThread));
  state->calling_class = reinterpret_cast<decltype(state->calling_class)>(symbols::Find(kCallingClass));
  state->verify_access = reinterpret_cast<decltype(state->verify_access)>(symbols::Find(kVerifyAccess));
  state->decode_global = reinterpret_cast<decltype(state->decode_global)>(symbols::Find(symbols::method_handle_lookup::kDecodeGlobal));
  if (!one || !many || !state->current_thread || !state->calling_class || !state->verify_access || !state->decode_global)
    return fail("missing named constructor history service");
  if (!state->env && vm->GetEnv(reinterpret_cast<void**>(&state->env), JVMTI_VERSION_1_2) != JNI_OK)
    return fail("constructor tag environment unavailable");
  if (jni->PushLocalFrame(32)) return fail("constructor metadata local frame unavailable");
  Locals locals{jni};
  auto klass = [&](const char* name) -> jclass {
    if (jni->ExceptionCheck()) return nullptr;
    jclass value = jni->FindClass(name);
    return value ? static_cast<jclass>(jni->NewGlobalRef(value)) : nullptr;
  };
  jclass executable = klass("java/lang/reflect/Executable"), constructor = klass("java/lang/reflect/Constructor");
  jclass type = klass("java/lang/Class"), accessible = klass("java/lang/reflect/AccessibleObject");
  if (!executable || !constructor || !type || !accessible) return fail("constructor metadata classes unavailable");
  auto field = [&](jclass owner, const char* name, const char* signature) -> jfieldID {
    jint count = 0; jfieldID* fields = nullptr; jfieldID found = nullptr;
    if (state->env->GetClassFields(owner, &count, &fields)) return nullptr;
    for (jint i = 0; i < count; ++i) {
      char *actual_name = nullptr, *actual_signature = nullptr;
      const auto result = state->env->GetFieldName(owner, fields[i], &actual_name, &actual_signature, nullptr);
      if (!result && actual_name && actual_signature && !std::strcmp(actual_name, name) &&
          !std::strcmp(actual_signature, signature)) found = fields[i];
      if (actual_name) state->env->Deallocate(reinterpret_cast<unsigned char*>(actual_name));
      if (actual_signature) state->env->Deallocate(reinterpret_cast<unsigned char*>(actual_signature));
      if (found) break;
    }
    state->env->Deallocate(reinterpret_cast<unsigned char*>(fields));
    return found;
  };
  state->target = field(executable, "artMethod", "J");
  state->declaring = field(executable, "declaringClass", "Ljava/lang/Class;");
  state->access = field(executable, "accessFlags", "I");
  state->flags = field(type, "accessFlags", "I");
  state->override_field = field(accessible, "override", "Z");
  state->parameters = jni->GetMethodID(constructor, "getParameterTypes", "()[Ljava/lang/Class;");
  if (jni->ExceptionCheck()) return fail("constructor parameter metadata unavailable");
  state->public_lookup = jni->GetMethodID(type, "getConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;");
  if (jni->ExceptionCheck()) return fail("constructor lookup metadata unavailable");
  state->declared_lookup = jni->GetMethodID(type, "getDeclaredConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;");
  state->class_type = type;
  for (jclass local : {executable, constructor, accessible}) jni->DeleteGlobalRef(local);
  if (jni->ExceptionCheck() || !state->target || !state->declaring || !state->access || !state->flags ||
      !state->override_field || !state->parameters || !state->public_lookup || !state->declared_lookup)
    return fail("constructor metadata declarations unavailable");
  state->instantiation = klass("java/lang/InstantiationException");
  state->missing = klass("java/lang/NoSuchMethodError");
  state->invocation = klass("java/lang/reflect/InvocationTargetException");
  state->illegal_argument = klass("java/lang/IllegalArgumentException");
  state->cast_error = klass("java/lang/ClassCastException");
  state->null_error = klass("java/lang/NullPointerException");
  state->initializer_error = klass("java/lang/ExceptionInInitializerError");
  state->linkage_error = klass("java/lang/NoClassDefFoundError");
  state->access_error = klass("java/lang/IllegalAccessException");
  state->internal_error = klass("java/lang/InternalError");
  state->out_of_memory = klass("java/lang/OutOfMemoryError");
  if (!state->instantiation || !state->missing || !state->invocation || !state->illegal_argument ||
      !state->cast_error || !state->null_error || !state->initializer_error || !state->linkage_error || !state->access_error || !state->internal_error || !state->out_of_memory)
    return fail("constructor exception classes unavailable");
  state->invocation_init = jni->GetMethodID(state->invocation, "<init>", "(Ljava/lang/Throwable;)V");
  if (jni->ExceptionCheck()) return fail("constructor wrapper metadata unavailable");
  state->argument_init = jni->GetMethodID(state->illegal_argument, "<init>", "(Ljava/lang/Throwable;)V");
  const char* boxed[] = {"java/lang/Boolean", "java/lang/Byte", "java/lang/Character", "java/lang/Short",
                         "java/lang/Integer", "java/lang/Long", "java/lang/Float", "java/lang/Double"};
  for (size_t i = 0; i < state->boxed.size(); ++i) {
    state->boxed[i] = klass(boxed[i]);
    if (!state->boxed[i]) return fail("constructor conversion classes unavailable");
    jfieldID field = jni->GetStaticFieldID(state->boxed[i], "TYPE", "Ljava/lang/Class;");
    if (!field) return fail("constructor conversion type unavailable");
    jobject primitive = jni->GetStaticObjectField(state->boxed[i], field);
    if (!primitive) return fail("constructor conversion primitive unavailable");
    state->primitive[i] = static_cast<jclass>(jni->NewGlobalRef(primitive));
    jni->DeleteLocalRef(primitive);
    if (!state->primitive[i]) return fail("constructor primitive root unavailable");
  }
  if (jni->ExceptionCheck() || !state->invocation_init || !state->argument_init) return fail("constructor exception metadata unavailable");
  auto identity = reinterpret_cast<decltype(state->same_object)>(symbols::Find(kSameObject));
  if (identity) {
    jweak weak = jni->NewWeakGlobalRef(state->primitive[0]);
    if (!weak) return fail("constructor weak identity probe unavailable");
    const bool valid = identity(jni, state->primitive[0], weak) && !identity(jni, state->primitive[1], weak);
    jni->DeleteWeakGlobalRef(weak);
    if (valid && !jni->ExceptionCheck()) state->same_object = identity;
    else if (jni->ExceptionCheck()) return fail("constructor weak identity probe failed");
  }
  jvmtiCapabilities capabilities{};
  capabilities.can_tag_objects = 1;
  capabilities.can_generate_object_free_events = 1;
  if (state->env->AddCapabilities(&capabilities)) return fail("constructor tag capabilities unavailable");
  if (!state->monitor && state->env->CreateRawMonitor("constructor history", &state->monitor)) return fail("constructor monitor unavailable");
  jvmtiEventCallbacks callbacks{};
  callbacks.ObjectFree = Freed;
  callbacks.ThreadEnd = [](jvmtiEnv*, JNIEnv* env, jthread) { Ended(env); };
  if (state->env->SetEventCallbacks(&callbacks, sizeof(callbacks)) ||
      state->env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_OBJECT_FREE, nullptr) ||
      state->env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_END, nullptr))
    return fail("constructor tag cleanup unavailable");
  state->one_target = one; state->one_name = one_name;
  state->many_target = many; state->many_name = many_name;
  append_hooks();
  state->ready.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::constructor_history

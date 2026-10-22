#include "platform/common/member_retirement.h"
#include "platform/common/method_retirement_filter.h"
#include "platform/common/continuation_profile.h"

#include <android/log.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>

#include "platform/symbols/member_retirement_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/common/invoke_kind.h"
#include "platform/common/constructor_history.h"
#include "evolution/class_schema.h"

namespace artdcevm::member_retirement {
namespace {
namespace abi = artdcevm::abi::arm64;
using abi::ArtField;
using abi::ArtMethodPrefix;
using abi::Handle;
using abi::Object;
using abi::ReflectionSourceInfo;
using abi::kAccObsoleteMethod;

template <typename T> T* Pointer(uint64_t address) {
  return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
bool Fail(std::string* why, std::string reason) {
  *why = std::move(reason);
  return false;
}

}  // namespace
struct RetiredFieldInfo {
  int kind;  // Index into kFieldKinds.
  bool is_static;
};
constexpr char kFieldKinds[] = {'Z', 'B', 'C', 'S', 'I', 'J', 'F', 'D', 'L'};
constexpr size_t kFieldKindCount = sizeof(kFieldKinds);
// Profile-independent state; real retired members stay rooted for process lifetime.
struct Process {
  std::atomic<bool> any_retired{false};
  std::atomic<bool> obsolete_flag_trusted{true};
  std::shared_mutex retired_mutex;
  MethodRetirementFilter retired_filter;
  std::set<const void*> retired_methods;
  std::map<jmethodID, const void*> retired_ids;
  std::mutex roots_mutex;
  std::vector<jobject> retained_roots;
  std::atomic<uint64_t> jni_retirement_guards{0};
  std::atomic<bool> any_retired_field{false};
  std::map<const void*, RetiredFieldInfo> retired_fields;
  std::set<jfieldID> retired_field_ids;
  std::set<const void*> placeholder_fields;
  bool field_retirement_available = false;
  bool field_ids_hooked = false;
  bool prepared = false;
  std::array<ArtField*, kFieldKindCount> placeholder_instance{};
  std::array<ArtField*, kFieldKindCount> placeholder_static{};
  void (*throw_exception)(void*, const char*, const char*) = nullptr;
  ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
  void* original_mh = nullptr;
  void* original_mh_exact = nullptr;
  void* original_mh_frame = nullptr;
  void* original_encode = nullptr;
  void* original_encode_handle = nullptr;
  std::atomic<int64_t> var_handle_modes_offset{-1};
  std::atomic<int64_t> handle_target_offset{-1};
  void* original_encode_field = nullptr;
  void* original_encode_field_handle = nullptr;
  jobject (*original_method_invoke)(JNIEnv*, jobject, jobject, jobjectArray) = nullptr;
  const JNINativeInterface* jni = nullptr;  // The table in force before ours.
  jclass no_such_method_error = nullptr;
  jclass no_such_field_error = nullptr;
  jclass invocation_target_exception = nullptr;
  jmethodID no_such_method_init = nullptr;
  jmethodID invocation_target_init = nullptr;
  jobject (*original_constructor_new_instance)(JNIEnv*, jobject, jobjectArray) = nullptr;
};
namespace {
// Created by the first Prepare and kept for the process lifetime: installed
// guards may still run after Install rolls back or degrades.
Process* process = nullptr;

// JVMTI-requested field IDs and their ArtFields, while resolving placeholders.
thread_local std::map<jfieldID, const void*>* field_capture = nullptr;

bool AnyRetired() { return process && process->any_retired.load(std::memory_order_acquire); }
bool ContainsMethod(const void* method) {
  if (!AnyRetired() || !process->retired_filter.MayContain(method)) return false;
  std::shared_lock<std::shared_mutex> lock(process->retired_mutex);
  return process->retired_methods.count(method) != 0;
}
// Never dereferences the ID, so CheckJNI can still diagnose invalid IDs.
const void* RetiredIdMethod(jmethodID id) {
  if (!AnyRetired()) return nullptr;
  std::shared_lock<std::shared_mutex> lock(process->retired_mutex);
  const auto found = process->retired_ids.find(id);
  if (found != process->retired_ids.end()) return found->second;
  return (reinterpret_cast<uintptr_t>(id) & 1) == 0 && process->retired_methods.count(id) ? id : nullptr;
}
bool ContainsId(jmethodID id) { return RetiredIdMethod(id) != nullptr; }
void Retire(const void* method) {
  {
    std::unique_lock<std::shared_mutex> lock(process->retired_mutex);
    process->retired_filter.Add(method);
    process->retired_methods.insert(method);
  }
  process->any_retired.store(true, std::memory_order_release);
}
void RecordId(const void* method, jmethodID id) {
  std::unique_lock<std::shared_mutex> lock(process->retired_mutex);
  if (process->retired_methods.count(method)) process->retired_ids[id] = method;
}
// Primitive kinds by descriptor; anything else is a reference.
int FieldKind(std::string_view descriptor) {
  for (size_t i = 0; i + 1 < kFieldKindCount; ++i)
    if (descriptor.size() == 1 && descriptor[0] == kFieldKinds[i]) return static_cast<int>(i);
  return kFieldKindCount - 1;
}

bool AnyRetiredField() { return process && process->any_retired_field.load(std::memory_order_acquire); }
std::optional<RetiredFieldInfo> FindRetiredField(const void* field) {
  if (!AnyRetiredField()) return std::nullopt;
  std::shared_lock<std::shared_mutex> lock(process->retired_mutex);
  const auto found = process->retired_fields.find(field);
  if (found == process->retired_fields.end()) return std::nullopt;
  return found->second;
}
// Retired or placeholder field, by ID; never dereferences the ID.
bool ContainsFieldId(jfieldID id) {
  if (!AnyRetiredField()) return false;
  std::shared_lock<std::shared_mutex> lock(process->retired_mutex);
  if (process->retired_field_ids.count(id)) return true;
  if (reinterpret_cast<uintptr_t>(id) & 1) return false;
  return process->retired_fields.count(id) || process->placeholder_fields.count(id);
}
void RecordFieldId(const void* field, jfieldID id) {
  if (field_capture) (*field_capture)[id] = field;
  if (!AnyRetiredField()) return;
  {
    std::shared_lock<std::shared_mutex> lock(process->retired_mutex);
    if (!process->retired_fields.count(field) && !process->placeholder_fields.count(field)) return;
  }
  std::unique_lock<std::shared_mutex> lock(process->retired_mutex);
  process->retired_field_ids.insert(id);
}

// Every retired member is an old method that ART marks obsolete in the same
// commit (checked after each Update), so live methods fail one relaxed read.
bool Retired(const void* method) {
  if (!method || !AnyRetired()) return false;
  if (process->obsolete_flag_trusted.load(std::memory_order_relaxed) &&
      !(static_cast<const ArtMethodPrefix*>(method)->access_flags.load(std::memory_order_relaxed) &
        kAccObsoleteMethod)) return false;
  return continuation_profile::RetiredLookup(method, ContainsMethod);
}

// ART services, resolved by name in Prepare.

void ThrowRetired(void* self) {
  process->throw_exception(self, "Ljava/lang/NoSuchMethodError;", "method retired by structural redefinition");
}

// VarHandle accessors check accessModesBitMask before any field access
// (VarHandleInvokeAccessorImpl and the JIT intrinsics), so a VarHandle for a
// retired field throws UnsupportedOperationException. The offset comes from
// ART's own field table.
void DisableVarHandle(const void* var_handle) {
  const auto* object = static_cast<const Object*>(var_handle);
  if (!object) return;
  int64_t offset = process->var_handle_modes_offset.load(std::memory_order_relaxed);
  if (offset < 0) {
    const ArtField* field = process->find_instance_field(Pointer<void>(object->klass), "accessModesBitMask", "I");
    if (!field) {
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "VarHandle.accessModesBitMask not found");
      return;
    }
    offset = field->offset;
    process->var_handle_modes_offset.store(offset, std::memory_order_relaxed);
  }
  // A primitive store into an object under the commit's suspend-all.
  std::memset(const_cast<uint8_t*>(static_cast<const uint8_t*>(var_handle)) + offset, 0, sizeof(int32_t));
}

// java.lang.invoke.MethodHandle.artFieldOrMethod, located through ART's own
// field table: an ArtMethod for invoke kinds, an ArtField for accessor kinds.
// Transformer handles carry neither, so the target is only looked up.
enum class HandleTarget { kLive, kRetiredMethod, kRetiredField };
HandleTarget RetiredHandle(Handle handle, const void** retired_method) {
  if (!AnyRetired() && !AnyRetiredField()) return HandleTarget::kLive;
  const auto* object = handle.Get<Object>();
  if (!object) return HandleTarget::kLive;
  int64_t offset = process->handle_target_offset.load(std::memory_order_acquire);
  if (offset < 0) {
    const ArtField* field = process->find_instance_field(Pointer<void>(object->klass), "artFieldOrMethod", "J");
    if (!field) {
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "MethodHandle.artFieldOrMethod not found");
      return HandleTarget::kLive;
    }
    offset = field->offset;
    process->handle_target_offset.store(offset, std::memory_order_release);
  }
  uint64_t target = 0;
  std::memcpy(&target, reinterpret_cast<const uint8_t*>(object) + offset, sizeof(target));
  if (ContainsMethod(Pointer<const void>(target))) {
    *retired_method = Pointer<const void>(target);
    return HandleTarget::kRetiredMethod;
  }
  if (FindRetiredField(Pointer<const void>(target))) return HandleTarget::kRetiredField;
  return HandleTarget::kLive;
}
// Throws for a retired handle target; false when the handle may run.
bool RejectHandle(void* self, Handle handle) {
  const void* retired_method = nullptr;
  switch (RetiredHandle(handle, &retired_method)) {
    case HandleTarget::kRetiredMethod:
      if (invoke_kind::UninstantiableConstructor(retired_method))
        process->throw_exception(self, "Ljava/lang/InstantiationException;", "declaring class is not instantiable");
      else ThrowRetired(self);
      return true;
    case HandleTarget::kRetiredField:
      process->throw_exception(self, "Ljava/lang/NoSuchFieldError;", "field retired by structural redefinition");
      return true;
    case HandleTarget::kLive: return false;
  }
  return false;
}
bool HandleCall(void* original, void* self, void* frame, Handle method, Handle type,
                const void* operands, uint64_t* result) {
  if (RejectHandle(self, method)) { if (result) *result = 0; return false; }
  using Function = bool (*)(void*, void*, Handle, Handle, const void*, uint64_t*);
  return reinterpret_cast<Function>(original)(self, frame, method, type, operands, result);
}
bool MethodHandleInvoke(void* self, void* frame, Handle method, Handle type,
                        const void* operands, uint64_t* result) noexcept {
  return HandleCall(process->original_mh, self, frame, method, type, operands, result);
}
bool MethodHandleInvokeExact(void* self, void* frame, Handle method, Handle type,
                             const void* operands, uint64_t* result) noexcept {
  return HandleCall(process->original_mh_exact, self, frame, method, type, operands, result);
}
void MethodHandleFrame(void* self, Handle method, Handle frame) noexcept {
  if (RejectHandle(self, method)) return;
  using Function = void (*)(void*, Handle, Handle);
  reinterpret_cast<Function>(process->original_mh_frame)(self, method, frame);
}
jmethodID Encode(void* manager, void* method) noexcept {
  using Function = jmethodID (*)(void*, void*);
  jmethodID id = reinterpret_cast<Function>(process->original_encode)(manager, method);
  if (Retired(method)) RecordId(method, id);
  return id;
}
struct ReflectiveHandle { void** reference; };
jmethodID EncodeHandle(void* manager, ReflectiveHandle method) noexcept {
  using Function = jmethodID (*)(void*, ReflectiveHandle);
  jmethodID id = reinterpret_cast<Function>(process->original_encode_handle)(manager, method);
  if (Retired(*method.reference)) RecordId(*method.reference, id);
  return id;
}
// JniIdManager::EncodeFieldId, in both ART spellings: record IDs encoded for
// retired or placeholder fields (FromReflectedField on a remapped Field).
jfieldID EncodeField(void* manager, void* field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, void*)>(process->original_encode_field)(manager, field);
  RecordFieldId(field, id);
  return id;
}
jfieldID EncodeFieldHandle(void* manager, ReflectiveHandle field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, ReflectiveHandle)>(
      process->original_encode_field_handle)(manager, field);
  RecordFieldId(*field.reference, id);
  return id;
}
jfieldID EncodeFieldWithClass(void* manager, Handle klass, void* field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, Handle, void*)>(process->original_encode_field)(
      manager, klass, field);
  RecordFieldId(field, id);
  return id;
}
jfieldID EncodeFieldHandleWithClass(void* manager, Handle klass, ReflectiveHandle field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, Handle, ReflectiveHandle)>(
      process->original_encode_field_handle)(manager, klass, field);
  RecordFieldId(*field.reference, id);
  return id;
}
// Reflection: Method.invoke's native entry, before ART checks the receiver
// against the old declaring class. Only public JNI identifies the target.
constexpr char kRetiredMessage[] = "method retired by structural redefinition";

void ThrowInstantiation(JNIEnv* env) {
  jclass exception = process->jni->FindClass(env, "java/lang/InstantiationException");
  if (exception) {
    process->jni->ThrowNew(env, exception, "declaring class is not instantiable");
    process->jni->DeleteLocalRef(env, exception);
  }
}

// A cached reflective Method or Constructor whose target retired: throw, as
// for a failing target, InvocationTargetException wrapping NoSuchMethodError.
bool RejectReflected(JNIEnv* env, jobject executable) {
  if (!AnyRetired()) return false;
  jmethodID id = process->jni->FromReflectedMethod(env, executable);
  const void* method = id ? RetiredIdMethod(id) : nullptr;
  if (!method) return false;
  process->jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  // Fast-native reflection is Runnable here. Check the current owner before
  // the missing constructor body, including after repeated kind conversions.
  if (invoke_kind::UninstantiableConstructor(method)) {
    ThrowInstantiation(env);
    return true;
  }
  jstring message = process->jni->NewStringUTF(env, kRetiredMessage);
  jobject cause = message ? process->jni->NewObject(env, process->no_such_method_error, process->no_such_method_init, message)
                          : nullptr;
  jobject wrapped = cause ? process->jni->NewObject(env, process->invocation_target_exception,
                                            process->invocation_target_init, cause)
                          : nullptr;
  if (wrapped) process->jni->Throw(env, static_cast<jthrowable>(wrapped));
  for (jobject local : {static_cast<jobject>(message), cause, wrapped})
    if (local) process->jni->DeleteLocalRef(env, local);
  return true;
}
jobject MethodInvoke(JNIEnv* env, jobject method, jobject receiver, jobjectArray args) noexcept {
  return RejectReflected(env, method) ? nullptr
                                      : process->original_method_invoke(env, method, receiver, args);
}
// A retired constructor would run its old code on an object of the new class.
jobject ConstructorNewInstanceLegacy(JNIEnv* env, jobject constructor, jobjectArray args) {
  return RejectReflected(env, constructor) ? nullptr
                                           : process->original_constructor_new_instance(env, constructor, args);
}
jobject ConstructorNewInstance(JNIEnv* env, jobject constructor, jobjectArray args) noexcept {
  return constructor_history::Invoke(env, constructor, args, process->original_constructor_new_instance,
                                     ConstructorNewInstanceLegacy);
}

// JNI field access with a retired or placeholder field ID.
bool RejectJniField(JNIEnv* env, jfieldID id) {
  if (!ContainsFieldId(id) || process->jni->ExceptionCheck(env)) return false;
  process->jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  process->jni->ThrowNew(env, process->no_such_field_error, "field retired by structural redefinition");
  return true;
}
#define ARTDCEVM_JNI_FIELDS(T, Name)                                                              \
  T Get##Name##Field(JNIEnv* env, jobject object, jfieldID id) {                                 \
    return RejectJniField(env, id) ? T() : process->jni->Get##Name##Field(env, object, id);               \
  }                                                                                              \
  void Set##Name##Field(JNIEnv* env, jobject object, jfieldID id, T value) {                     \
    if (!RejectJniField(env, id)) process->jni->Set##Name##Field(env, object, id, value);                 \
  }                                                                                              \
  T GetStatic##Name##Field(JNIEnv* env, jclass klass, jfieldID id) {                             \
    return RejectJniField(env, id) ? T() : process->jni->GetStatic##Name##Field(env, klass, id);          \
  }                                                                                              \
  void SetStatic##Name##Field(JNIEnv* env, jclass klass, jfieldID id, T value) {                 \
    if (!RejectJniField(env, id)) process->jni->SetStatic##Name##Field(env, klass, id, value);            \
  }
ARTDCEVM_JNI_FIELDS(jobject, Object)
ARTDCEVM_JNI_FIELDS(jboolean, Boolean)
ARTDCEVM_JNI_FIELDS(jbyte, Byte)
ARTDCEVM_JNI_FIELDS(jchar, Char)
ARTDCEVM_JNI_FIELDS(jshort, Short)
ARTDCEVM_JNI_FIELDS(jint, Int)
ARTDCEVM_JNI_FIELDS(jlong, Long)
ARTDCEVM_JNI_FIELDS(jfloat, Float)
ARTDCEVM_JNI_FIELDS(jdouble, Double)
#undef ARTDCEVM_JNI_FIELDS
jobject ToReflectedField(JNIEnv* env, jclass klass, jfieldID id, jboolean is_static) {
  return RejectJniField(env, id) ? nullptr : process->jni->ToReflectedField(env, klass, id, is_static);
}

// JNI Call*Method*: a JVMTI function-table override in front of the previous
// table (CheckJNI's when enabled), so live calls keep every original check.
// The ID is only looked up, never decoded: CheckJNI still reports invalid IDs.
bool RejectJni(JNIEnv* env, jmethodID id) {
  if (!ContainsId(id) || process->jni->ExceptionCheck(env)) return false;
  process->jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  process->jni->ThrowNew(env, process->no_such_method_error, kRetiredMessage);
  return true;
}
bool RejectJniConstructor(JNIEnv* env, jclass klass, jmethodID id) {
  if (!invoke_kind::ClassKindProtection()) return RejectJni(env, id);
  if (!ContainsId(id) || process->jni->ExceptionCheck(env)) return false;
  // A JNI-table override may run while Native. Use public JNI to inspect the
  // rooted current Class; managed-field reads require a Runnable ART entry.
  jclass class_type = process->jni->GetObjectClass(env, klass);
  jmethodID modifiers = class_type ? process->jni->GetMethodID(env, class_type, "getModifiers", "()I") : nullptr;
  jint flags = modifiers ? process->jni->CallIntMethod(env, klass, modifiers) : 0;
  if (class_type) process->jni->DeleteLocalRef(env, class_type);
  if (process->jni->ExceptionCheck(env)) return true;
  if (flags & evolution::kAccAbstract) {
    process->jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
    ThrowInstantiation(env);
    return true;
  }
  return RejectJni(env, id);
}
#define ARTDCEVM_JNI_RESULT_CALLS(T, Name)                                                       \
  T Call##Name##Method(JNIEnv* env, jobject object, jmethodID id, ...) {                         \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = process->jni->Call##Name##MethodV(env, object, id, args);                                  \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T Call##Name##MethodV(JNIEnv* env, jobject object, jmethodID id, va_list args) {               \
    return RejectJni(env, id) ? T() : process->jni->Call##Name##MethodV(env, object, id, args);           \
  }                                                                                              \
  T Call##Name##MethodA(JNIEnv* env, jobject object, jmethodID id, const jvalue* args) {         \
    return RejectJni(env, id) ? T() : process->jni->Call##Name##MethodA(env, object, id, args);           \
  }                                                                                              \
  T CallNonvirtual##Name##Method(JNIEnv* env, jobject object, jclass klass, jmethodID id, ...) { \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = process->jni->CallNonvirtual##Name##MethodV(env, object, klass, id, args);                 \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T CallNonvirtual##Name##MethodV(JNIEnv* env, jobject object, jclass klass, jmethodID id,       \
                                  va_list args) {                                                \
    return RejectJni(env, id) ? T()                                                              \
                              : process->jni->CallNonvirtual##Name##MethodV(env, object, klass, id, args);\
  }                                                                                              \
  T CallNonvirtual##Name##MethodA(JNIEnv* env, jobject object, jclass klass, jmethodID id,       \
                                  const jvalue* args) {                                          \
    return RejectJni(env, id) ? T()                                                              \
                              : process->jni->CallNonvirtual##Name##MethodA(env, object, klass, id, args);\
  }                                                                                              \
  T CallStatic##Name##Method(JNIEnv* env, jclass klass, jmethodID id, ...) {                     \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = process->jni->CallStatic##Name##MethodV(env, klass, id, args);                             \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T CallStatic##Name##MethodV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {           \
    return RejectJni(env, id) ? T() : process->jni->CallStatic##Name##MethodV(env, klass, id, args);      \
  }                                                                                              \
  T CallStatic##Name##MethodA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {     \
    return RejectJni(env, id) ? T() : process->jni->CallStatic##Name##MethodA(env, klass, id, args);      \
  }
ARTDCEVM_JNI_RESULT_CALLS(jobject, Object)
ARTDCEVM_JNI_RESULT_CALLS(jboolean, Boolean)
ARTDCEVM_JNI_RESULT_CALLS(jbyte, Byte)
ARTDCEVM_JNI_RESULT_CALLS(jchar, Char)
ARTDCEVM_JNI_RESULT_CALLS(jshort, Short)
ARTDCEVM_JNI_RESULT_CALLS(jint, Int)
ARTDCEVM_JNI_RESULT_CALLS(jlong, Long)
ARTDCEVM_JNI_RESULT_CALLS(jfloat, Float)
ARTDCEVM_JNI_RESULT_CALLS(jdouble, Double)
#undef ARTDCEVM_JNI_RESULT_CALLS

jobject NewObject(JNIEnv* env, jclass klass, jmethodID id, ...) {
  if (RejectJniConstructor(env, klass, id)) return nullptr;
  va_list args;
  va_start(args, id);
  jobject result = process->jni->NewObjectV(env, klass, id, args);
  va_end(args);
  return result;
}
jobject NewObjectV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {
  return RejectJniConstructor(env, klass, id) ? nullptr : process->jni->NewObjectV(env, klass, id, args);
}
jobject NewObjectA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {
  return RejectJniConstructor(env, klass, id) ? nullptr : process->jni->NewObjectA(env, klass, id, args);
}

void CallVoidMethod(JNIEnv* env, jobject object, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  process->jni->CallVoidMethodV(env, object, id, args);
  va_end(args);
}
void CallVoidMethodV(JNIEnv* env, jobject object, jmethodID id, va_list args) {
  if (!RejectJni(env, id)) process->jni->CallVoidMethodV(env, object, id, args);
}
void CallVoidMethodA(JNIEnv* env, jobject object, jmethodID id, const jvalue* args) {
  if (!RejectJni(env, id)) process->jni->CallVoidMethodA(env, object, id, args);
}
void CallNonvirtualVoidMethod(JNIEnv* env, jobject object, jclass klass, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  process->jni->CallNonvirtualVoidMethodV(env, object, klass, id, args);
  va_end(args);
}
void CallNonvirtualVoidMethodV(JNIEnv* env, jobject object, jclass klass, jmethodID id,
                               va_list args) {
  if (!RejectJni(env, id)) process->jni->CallNonvirtualVoidMethodV(env, object, klass, id, args);
}
void CallNonvirtualVoidMethodA(JNIEnv* env, jobject object, jclass klass, jmethodID id,
                               const jvalue* args) {
  if (!RejectJni(env, id)) process->jni->CallNonvirtualVoidMethodA(env, object, klass, id, args);
}
void CallStaticVoidMethod(JNIEnv* env, jclass klass, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  process->jni->CallStaticVoidMethodV(env, klass, id, args);
  va_end(args);
}
void CallStaticVoidMethodV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {
  if (!RejectJni(env, id)) process->jni->CallStaticVoidMethodV(env, klass, id, args);
}
void CallStaticVoidMethodA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {
  if (!RejectJni(env, id)) process->jni->CallStaticVoidMethodA(env, klass, id, args);
}

JNINativeInterface guarded_jni;
#define ARTDCEVM_JNI_ENTRIES(Name)                                                                \
  guarded_jni.Call##Name##Method = Call##Name##Method;                                           \
  guarded_jni.Call##Name##MethodV = Call##Name##MethodV;                                         \
  guarded_jni.Call##Name##MethodA = Call##Name##MethodA;                                         \
  guarded_jni.CallNonvirtual##Name##Method = CallNonvirtual##Name##Method;                       \
  guarded_jni.CallNonvirtual##Name##MethodV = CallNonvirtual##Name##MethodV;                     \
  guarded_jni.CallNonvirtual##Name##MethodA = CallNonvirtual##Name##MethodA;                     \
  guarded_jni.CallStatic##Name##Method = CallStatic##Name##Method;                               \
  guarded_jni.CallStatic##Name##MethodV = CallStatic##Name##MethodV;                             \
  guarded_jni.CallStatic##Name##MethodA = CallStatic##Name##MethodA;

// Caches the exception classes, then overrides the JNI table. The override
// also disables ART's Core Platform API checks (JNIEnvExt::SetTableOverride),
// which apply to platform callers only. Our entries forward until a retirement.
bool InstallJniTable(JavaVM* vm, jvmtiEnv* jvmti, std::string* failure) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
    return Fail(failure, "no JNIEnv for the JNI guards");
  auto global_class = [&](const char* name) -> jclass {
    jclass local = env->FindClass(name);
    if (!local) { env->ExceptionClear(); return nullptr; }
    auto global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
  };
  process->no_such_method_error = global_class("java/lang/NoSuchMethodError");
  process->invocation_target_exception = global_class("java/lang/reflect/InvocationTargetException");
  process->no_such_field_error = global_class("java/lang/NoSuchFieldError");
  if (!process->no_such_method_error || !process->invocation_target_exception || !process->no_such_field_error)
    return Fail(failure, "exception classes unavailable");
  process->no_such_method_init = env->GetMethodID(process->no_such_method_error, "<init>", "(Ljava/lang/String;)V");
  process->invocation_target_init = env->GetMethodID(process->invocation_target_exception, "<init>",
                                            "(Ljava/lang/Throwable;)V");
  if (!process->no_such_method_init || !process->invocation_target_init) {
    env->ExceptionClear();
    return Fail(failure, "exception constructors unavailable");
  }
  jniNativeInterface* current = nullptr;
  if (jvmti->GetJNIFunctionTable(&current) != JVMTI_ERROR_NONE || !current)
    return Fail(failure, "GetJNIFunctionTable failed");
  process->jni = current;  // Kept for the process lifetime; the override forwards to it.
  guarded_jni = *current;
  ARTDCEVM_JNI_ENTRIES(Object)
  ARTDCEVM_JNI_ENTRIES(Boolean)
  ARTDCEVM_JNI_ENTRIES(Byte)
  ARTDCEVM_JNI_ENTRIES(Char)
  ARTDCEVM_JNI_ENTRIES(Short)
  ARTDCEVM_JNI_ENTRIES(Int)
  ARTDCEVM_JNI_ENTRIES(Long)
  ARTDCEVM_JNI_ENTRIES(Float)
  ARTDCEVM_JNI_ENTRIES(Double)
  ARTDCEVM_JNI_ENTRIES(Void)
#define ARTDCEVM_JNI_FIELD_ENTRIES(Name)                                                          \
  guarded_jni.Get##Name##Field = Get##Name##Field;                                               \
  guarded_jni.Set##Name##Field = Set##Name##Field;                                               \
  guarded_jni.GetStatic##Name##Field = GetStatic##Name##Field;                                   \
  guarded_jni.SetStatic##Name##Field = SetStatic##Name##Field;
  ARTDCEVM_JNI_FIELD_ENTRIES(Object)
  ARTDCEVM_JNI_FIELD_ENTRIES(Boolean)
  ARTDCEVM_JNI_FIELD_ENTRIES(Byte)
  ARTDCEVM_JNI_FIELD_ENTRIES(Char)
  ARTDCEVM_JNI_FIELD_ENTRIES(Short)
  ARTDCEVM_JNI_FIELD_ENTRIES(Int)
  ARTDCEVM_JNI_FIELD_ENTRIES(Long)
  ARTDCEVM_JNI_FIELD_ENTRIES(Float)
  ARTDCEVM_JNI_FIELD_ENTRIES(Double)
#undef ARTDCEVM_JNI_FIELD_ENTRIES
  guarded_jni.ToReflectedField = ToReflectedField;
  guarded_jni.NewObject = NewObject;
  guarded_jni.NewObjectV = NewObjectV;
  guarded_jni.NewObjectA = NewObjectA;
  if (jvmti->SetJNIFunctionTable(&guarded_jni) != JVMTI_ERROR_NONE)
    return Fail(failure, "SetJNIFunctionTable failed");
  return true;
}
#undef ARTDCEVM_JNI_ENTRIES
}  // namespace

abi::ArtMethod* Visitor::VisitMethod(abi::ArtMethod* method, const ReflectionSourceInfo& info) noexcept {
  const bool retired = ContainsMethod(method);
  if (info.type == abi::kSourceDexCacheResolvedMethod) {
    const auto& source = reinterpret_cast<const abi::DexCacheSourceInfo&>(info);
    // Canonical mappings preserve the parent declaration for retained members.
    // A DEX entry, however, belongs to its symbolic owner: the old child may
    // no longer inherit that method or may now declare a private/static one.
    // Let ART invalidate the entry and resolve that symbol through its normal
    // resolver on the next invocation. Do not retire the unchanged parent.
    if (retired || cleared_.count(method) || (interface_symbols_ &&
        invoke_kind::InterfaceResolution(reinterpret_cast<void*>(source.cache), source.index)))
      return nullptr;
  }
  if (retired && info.type == abi::kSourceJniMethodId)
    RecordId(method, reinterpret_cast<const abi::JniMethodIdSourceInfo&>(info).id);
  return original_->VisitMethod(method, info);
}
ArtField* Visitor::VisitField(ArtField* field, const ReflectionSourceInfo& info) noexcept {
  const auto retired = FindRetiredField(field);
  if (!retired) return original_->VisitField(field, info);
  switch (info.type) {
    case abi::kSourceDexCacheResolvedField:
      return nullptr;  // Re-resolution throws NoSuchFieldError.
    case abi::kSourceJniFieldId:
      RecordFieldId(field, reinterpret_cast<const abi::JniFieldIdSourceInfo&>(info).id);
      return field;
    case abi::kSourceJavaLangReflectField:
      // ReplaceReferences would retarget the Field's declaring class to the
      // new class while its index and offset still describe the old field.
      return (retired->is_static ? process->placeholder_static : process->placeholder_instance)[retired->kind];
    case abi::kSourceJavaLangInvokeFieldVarHandle:
      DisableVarHandle(reinterpret_cast<const abi::HeapSourceInfo&>(info).source);
      return field;
    default:
      return field;  // MethodHandles are guarded; internal handles stay valid.
  }
}

bool Prepare(const Services& services, std::vector<Hook>* hooks, std::string* failure) {
  if (!process) process = new Process;
  process->prepared = false;
  process->find_instance_field = services.find_instance_field;
  using namespace symbols::structural;
  using namespace symbols::member_retirement;
  struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Required required[] = {
      {kMethodHandleInvoke, HookAddress(MethodHandleInvoke), &process->original_mh},
      {kMethodHandleInvokeExact, HookAddress(MethodHandleInvokeExact), &process->original_mh_exact},
      {kEncodeMethodId, HookAddress(Encode), &process->original_encode},
      {kEncodeMethodIdHandle, HookAddress(EncodeHandle), &process->original_encode_handle},
      {kThrowNewException, nullptr, HookSlot(&process->throw_exception)}};
  std::vector<Hook> resolved;
  for (const auto& entry : required) {
    void* address = symbols::Find(entry.symbol);
    if (!address) return Fail(failure, std::string("missing ") + entry.symbol.name);
    if (entry.replacement) resolved.push_back({address, entry.replacement, entry.original, entry.symbol.name});
    else *entry.original = address;
  }
  void* method_invoke = nullptr;
  const char* method_invoke_name = nullptr;
  for (const auto& symbol : kMethodInvokeNative) {
    if ((method_invoke = symbols::Find(symbol))) { method_invoke_name = symbol.name; break; }
  }
  if (!method_invoke) return Fail(failure, "missing Method.invoke native entry");
  resolved.push_back({method_invoke, HookAddress(MethodInvoke), HookSlot(&process->original_method_invoke),
                      method_invoke_name});
  void* new_instance = nullptr;
  const char* new_instance_name = nullptr;
  for (const auto& symbol : kConstructorNewInstanceNative) {
    if ((new_instance = symbols::Find(symbol))) { new_instance_name = symbol.name; break; }
  }
  if (!new_instance) return Fail(failure, "missing Constructor.newInstance0 native entry");
  resolved.push_back({new_instance, HookAddress(ConstructorNewInstance),
                      HookSlot(&process->original_constructor_new_instance), new_instance_name});
  // Field IDs: one of ART's two EncodeFieldId pairs. Without either, field
  // retirement stays off and methods still retire.
  struct EncodePair {
    symbols::NamedSymbol plain, handle;
    void* plain_replacement;
    void* handle_replacement;
  };
  const EncodePair encode_pairs[] = {
      {kEncodeFieldId, kEncodeFieldIdHandle, HookAddress(EncodeField), HookAddress(EncodeFieldHandle)},
      {kEncodeFieldIdWithClass, kEncodeFieldIdHandleWithClass, HookAddress(EncodeFieldWithClass),
       HookAddress(EncodeFieldHandleWithClass)}};
  process->field_ids_hooked = false;
  for (const auto& pair : encode_pairs) {
    void* plain = symbols::Find(pair.plain);
    void* handle = symbols::Find(pair.handle);
    if (!plain || !handle) continue;
    resolved.push_back({plain, pair.plain_replacement, &process->original_encode_field, pair.plain.name});
    resolved.push_back({handle, pair.handle_replacement, &process->original_encode_field_handle,
                        pair.handle.name});
    process->field_ids_hooked = true;
    break;
  }
  if (void* frame = symbols::Find(kMethodHandleInvokeExactWithFrame))
    resolved.push_back({frame, HookAddress(MethodHandleFrame), &process->original_mh_frame,
                        kMethodHandleInvokeExactWithFrame.name});
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  process->prepared = true;
  return true;
}
bool InstallJniGuards(JavaVM* vm, jvmtiEnv* jvmti, std::string* failure) {
  return process->prepared && InstallJniTable(vm, jvmti, failure);
}
bool FieldRetirementAvailable() { return process && process->field_retirement_available; }

RetiredField RetiredField::Of(ArtField* field, std::string_view descriptor, bool is_static) {
  return {field, FieldKind(descriptor), is_static};
}
bool RetiredField::IsStaticReference() const {
  return is_static && kind == static_cast<int>(kFieldKindCount - 1);
}
void RetireFields(const std::vector<RetiredField>& fields) {
  if (fields.empty()) return;
  {
    std::unique_lock<std::shared_mutex> lock(process->retired_mutex);
    for (const auto& retired : fields) process->retired_fields[retired.field] = {retired.kind, retired.is_static};
  }
  process->any_retired_field.store(true, std::memory_order_release);
}
bool IsRetiredMethod(const void* method) { return Retired(method); }
void RegisterJniAlias(const void* method, jmethodID id) {
  if (ContainsMethod(method)) RecordId(method, id);
}
void RetireMethod(const void* method) { Retire(method); }
void ConfirmObsolete(const std::vector<const void*>& methods) {
  // ART has now marked every old method obsolete; the guards' fast path
  // relies on that flag, so stop using it if this runtime disagrees.
  for (const void* method : methods) {
    if (!(static_cast<const ArtMethodPrefix*>(method)->access_flags.load() & kAccObsoleteMethod)) {
      process->obsolete_flag_trusted.store(false);
      __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", "obsolete flag not observed; guards use lookups");
      break;
    }
  }
}

namespace {
// ArtFields of one placeholder class, from JVMTI (which does not initialize
// the class) and the IDs its encoding recorded.
bool ResolvePlaceholders(jvmtiEnv* jvmti, jclass klass, const std::map<jfieldID, const void*>& ids,
                         std::array<ArtField*, kFieldKindCount>* out, std::string* failure) {
  jint count = 0;
  jfieldID* fields = nullptr;
  if (jvmti->GetClassFields(klass, &count, &fields) != JVMTI_ERROR_NONE)
    return Fail(failure, "GetClassFields failed for a field placeholder class");
  bool resolved = true;
  for (jint i = 0; i < count && resolved; ++i) {
    char* name = nullptr;
    char* descriptor = nullptr;
    if (jvmti->GetFieldName(klass, fields[i], &name, &descriptor, nullptr) != JVMTI_ERROR_NONE) {
      resolved = false;
      break;
    }
    const auto found = ids.find(fields[i]);
    const void* field = found != ids.end() ? found->second
        : (reinterpret_cast<uintptr_t>(fields[i]) & 1) ? nullptr : fields[i];
    auto& slot = (*out)[FieldKind(descriptor)];
    resolved = field && !slot;
    slot = static_cast<ArtField*>(const_cast<void*>(field));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
  }
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
  for (ArtField* field : *out) resolved &= field != nullptr;
  return resolved ? true : Fail(failure, "field placeholders unresolved");
}
}  // namespace

void ResolveFieldPlaceholders(jvmtiEnv* jvmti, const FieldPlaceholders& placeholders,
                              std::string* note) {
  process->field_retirement_available = false;
  if (!process->prepared || !process->field_ids_hooked) {
    *note = "field retirement unavailable: no JniIdManager::EncodeFieldId pair";
    return;
  }
  if (!placeholders.jni || !placeholders.instance_fields || !placeholders.static_fields) {
    *note = "field retirement unavailable: placeholder classes missing";
    return;
  }
  std::map<jfieldID, const void*> ids;
  field_capture = &ids;
  std::string failure;
  const bool resolved =
      ResolvePlaceholders(jvmti, placeholders.instance_fields, ids, &process->placeholder_instance, &failure) &&
      ResolvePlaceholders(jvmti, placeholders.static_fields, ids, &process->placeholder_static, &failure);
  field_capture = nullptr;
  if (!resolved) {
    *note = "field retirement unavailable: " + failure;
    return;
  }
  {
    std::unique_lock<std::shared_mutex> lock(process->retired_mutex);
    for (const auto* table : {&process->placeholder_instance, &process->placeholder_static})
      for (ArtField* field : *table) process->placeholder_fields.insert(field);
    for (const auto& [id, field] : ids) process->retired_field_ids.insert(id);
  }
  // Fail RetiredStaticField's initializer once, so every reflective access to
  // a retired static field reports the same NoClassDefFoundError.
  JNIEnv* env = placeholders.jni;
  if (!env->GetStaticFieldID(placeholders.static_fields, "retiredInt", "I") && env->ExceptionCheck())
    env->ExceptionClear();
  process->field_retirement_available = true;
}

void RetainRoots(std::vector<jobject> roots) {
  std::lock_guard<std::mutex> lock(process->roots_mutex);
  process->retained_roots.insert(process->retained_roots.end(), roots.begin(), roots.end());
}
bool Rooted(JNIEnv* jni, jclass klass) {
  std::vector<jobject> roots;
  {
    std::lock_guard<std::mutex> lock(process->roots_mutex);
    roots = process->retained_roots;
  }
  for (jobject root : roots) if (jni->IsSameObject(root, klass)) return true;
  return false;
}
uint64_t JniRetirementGuardHits() {
  return process ? process->jni_retirement_guards.load(std::memory_order_relaxed) : 0;
}
}  // namespace artdcevm::member_retirement

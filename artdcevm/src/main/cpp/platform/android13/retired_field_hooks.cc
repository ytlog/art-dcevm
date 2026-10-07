#include "platform/android13/retired_field_hooks.h"
#include "platform/android13/member_decode.h"
#include "platform/symbols/android13.h"
#include <dlfcn.h>
#include <jni.h>
#include <vector>
#include <type_traits>

namespace artdcevm::android13 {
namespace {
bool (*guard_receiver_jni)(JNIEnv*, jobject, jfieldID);
bool (*guard_receiver_runnable)(const void*, jobject, jfieldID);
bool (*guard_varhandle)(void*, int, void*, const void*, void*);
bool (*guard_method_handle_put)(void*);
bool (*guard_final_put)(void*, void*, uint16_t*);
void (*visit_field)(void*, void*);
void* original_field;
void* original_visit;
void* original_varhandle;
void* original_method_handle_put;
void* original_check;
void* original_nterp_instance;
void* original_nterp_static;
struct Entry { std::string symbol; void* proxy; void** original; void* target = nullptr; };

void* GetField(void* field) { return DecodeReflectedField(field); }
void VisitField(void* field, void* visitor) { visit_field(field, visitor); }
bool VarHandle(void* handle, int mode, void* frame, const void* operands, void* result) {
  if (guard_varhandle(handle, mode, frame, operands, result)) return false;
  using Fn = bool (*)(void*, int, void*, const void*, void*);
  return reinterpret_cast<Fn>(original_varhandle)(handle, mode, frame, operands, result);
}
bool MethodHandleFieldPut(void* thread, void* frame, void* receiver, void* field,
                          int primitive, void* value) {
  if (guard_method_handle_put(field)) return false;
  using Fn = bool (*)(void*, void*, void*, void*, int, void*);
  return reinterpret_cast<Fn>(original_method_handle_put)(
      thread, frame, receiver, field, primitive, value);
}
bool CheckField(void* check, void* access, jobject receiver, jfieldID field,
                bool is_static, int primitive) {
  if (GuardRunnableFieldId(field)) return false;
  if (!is_static && guard_receiver_runnable(access, receiver, field)) return false;
  using Fn = bool (*)(void*, void*, jobject, jfieldID, bool, int);
  return reinterpret_cast<Fn>(original_check)(check, access, receiver, field, is_static, primitive);
}
uint32_t NterpInstanceField(void* thread, void* caller, uint16_t* dex_pc, size_t resolve_type) {
  if (guard_final_put(thread, caller, dex_pc)) return 0;
  using Fn = uint32_t (*)(void*, void*, uint16_t*, size_t);
  return reinterpret_cast<Fn>(original_nterp_instance)(thread, caller, dex_pc, resolve_type);
}
size_t NterpStaticField(void* thread, void* caller, uint16_t* dex_pc, size_t resolve_type) {
  if (guard_final_put(thread, caller, dex_pc)) return 0;
  using Fn = size_t (*)(void*, void*, uint16_t*, size_t);
  return reinterpret_cast<Fn>(original_nterp_static)(thread, caller, dex_pc, resolve_type);
}

// Typed callbacks preserve the AArch64 floating-point and integer return ABIs.
template <int indexed, typename T, bool is_static> struct JniField {
  using Receiver = std::conditional_t<is_static, jclass, jobject>;
  static inline void* original_get;
  static inline void* original_set;
  static T Get(JNIEnv* env, Receiver object, jfieldID field) {
    if (GuardNativeFieldId(env, field)) return T{};
    if constexpr (!is_static) {
      if (guard_receiver_jni(env, object, field)) return T{};
    }
    using Fn = T (*)(JNIEnv*, Receiver, jfieldID);
    return reinterpret_cast<Fn>(original_get)(env, object, field);
  }
  static void Set(JNIEnv* env, Receiver object, jfieldID field, T value) {
    if (GuardNativeFieldId(env, field)) return;
    if constexpr (!is_static) {
      if (guard_receiver_jni(env, object, field)) return;
    }
    using Fn = void (*)(JNIEnv*, Receiver, jfieldID, T);
    reinterpret_cast<Fn>(original_set)(env, object, field, value);
  }
  static void Add(std::vector<Entry>& entries, const char* type, const char* code) {
    entries.push_back({cas::JniFieldSymbol(indexed, is_static, false, type, code,
                                          std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Get), &original_get});
    entries.push_back({cas::JniFieldSymbol(indexed, is_static, true, type, code,
                                          std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Set), &original_set});
  }
};
template <typename T> struct ReflectionField {
  static inline void* original_get;
  static inline void* original_set;
  static T Get(JNIEnv* env, jobject field, jobject object) {
    if (GuardNativeReflectedField(env, field)) return T{};
    using Fn = T (*)(JNIEnv*, jobject, jobject);
    return reinterpret_cast<Fn>(original_get)(env, field, object);
  }
  static void Set(JNIEnv* env, jobject field, jobject object, T value) {
    if (GuardNativeReflectedField(env, field)) return;
    using Fn = void (*)(JNIEnv*, jobject, jobject, T);
    reinterpret_cast<Fn>(original_set)(env, field, object, value);
  }
  static void Add(std::vector<Entry>& entries, const char* type, const char* code) {
    entries.push_back({cas::ReflectionFieldSymbol(false, type, code,
                                                  std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Get), &original_get});
    entries.push_back({cas::ReflectionFieldSymbol(true, type, code,
                                                  std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Set), &original_set});
  }
};
}
bool InstallRetiredFieldHooks(HookEngine& hooks, void* library, std::string* error) {
  guard_receiver_jni = reinterpret_cast<decltype(guard_receiver_jni)>(dlsym(library, "ArtDcevmGuardJniFieldReceiver"));
  guard_receiver_runnable = reinterpret_cast<decltype(guard_receiver_runnable)>(dlsym(library, "ArtDcevmGuardRunnableFieldReceiver"));
  guard_varhandle = reinterpret_cast<decltype(guard_varhandle)>(dlsym(library, "ArtDcevmGuardFieldVarHandle"));
  guard_method_handle_put = reinterpret_cast<decltype(guard_method_handle_put)>(
      dlsym(library, "ArtDcevmGuardMethodHandleFieldPut"));
  guard_final_put = reinterpret_cast<decltype(guard_final_put)>(dlsym(library, "ArtDcevmGuardNterpFinalFieldPut"));
  visit_field = reinterpret_cast<decltype(visit_field)>(dlsym(library, "ArtDcevmSourceVisitField"));
  if (!guard_receiver_jni || !guard_receiver_runnable ||
      !guard_varhandle || !guard_method_handle_put ||
      !guard_final_put || !visit_field) {
    *error = "Retired field guard exports are incomplete"; return false;
  }
  std::vector<Entry> entries{
    {cas::kRetiredFieldHooks[0], reinterpret_cast<void*>(GetField), &original_field},
    {cas::kRetiredFieldHooks[1], reinterpret_cast<void*>(VisitField), &original_visit},
    {cas::kRetiredFieldHooks[2], reinterpret_cast<void*>(VarHandle), &original_varhandle},
    {cas::kRetiredFieldHooks[3], reinterpret_cast<void*>(MethodHandleFieldPut), &original_method_handle_put},
    {cas::kRetiredFieldHooks[4], reinterpret_cast<void*>(CheckField), &original_check},
    {cas::kRetiredFieldHooks[5], reinterpret_cast<void*>(NterpInstanceField), &original_nterp_instance},
    {cas::kRetiredFieldHooks[6], reinterpret_cast<void*>(NterpStaticField), &original_nterp_static},
  };
#define ADD(TYPE, JNI_TYPE, CODE) \
  JniField<0, JNI_TYPE, false>::Add(entries, #TYPE, CODE); \
  JniField<1, JNI_TYPE, false>::Add(entries, #TYPE, CODE); \
  JniField<0, JNI_TYPE, true>::Add(entries, #TYPE, CODE); \
  JniField<1, JNI_TYPE, true>::Add(entries, #TYPE, CODE); \
  ReflectionField<JNI_TYPE>::Add(entries, #TYPE, CODE)
  ADD(Boolean, jboolean, "h"); ADD(Byte, jbyte, "a"); ADD(Char, jchar, "t");
  ADD(Short, jshort, "s"); ADD(Int, jint, "i"); ADD(Long, jlong, "l");
  ADD(Float, jfloat, "f"); ADD(Double, jdouble, "d"); ADD(Object, jobject, "");
#undef ADD
  for (auto& entry : entries) {
    entry.target = hooks.FindSymbol(symbols::LibraryName(symbols::Library::kArt), entry.symbol.c_str());
    if (!entry.target) { *error = "Missing field symbol: " + entry.symbol; return false; }
  }
  std::vector<void*> tokens;
  for (const auto& entry : entries) {
    void* token = hooks.Install(entry.target, entry.proxy, entry.original, error);
    if (!token) {
      for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        std::string rollback;
        if (!hooks.Remove(*it, &rollback)) *error += "; unhook failed: " + rollback;
      }
      return false;
    }
    tokens.push_back(token);
  }
  return true;
}
}

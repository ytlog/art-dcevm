#include "platform/android13/retired_method_hooks.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/android13/member_decode.h"
#include "platform/android13/nterp_dispatch.h"
#include "platform/symbols/android13.h"
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <dlfcn.h>
#include <jni.h>
#include <vector>

namespace artdcevm::android13 {
namespace {
using GuardHandle = bool (*)(void*, void*, void*);
GuardHandle guard_handle;
bool (*guard_id)(jmethodID);
bool (*guard_jni_id)(JNIEnv*, jmethodID);
bool (*guard_receiver_method)(const void*, jobject, void*);
bool (*guard_receiver_id)(const void*, jobject, jmethodID);
bool (*guard_jni_executable)(JNIEnv*, jobject);
void* original_nterp;
void* original_deopt;
void (*trace_deopt)(void*, void*, void*, bool, int);
void* original_resolve;
void* original_resolve_handles;
void* checked_resolve;
void* checked_resolve_handles;
void* original_invoke;
void* original_jvalues;
void* original_varargs;
void* original_id_jvalues;
void* original_id_varargs;
void* original_direct_jvalues;
void* original_direct_varargs;
void* original_direct_id_jvalues;
void* original_direct_id_varargs;
void* original_reflection;
void* original_handle;
void* original_exact;
void* original_frame;
void* original_obsolete;
void* original_check_jni;
void* original_check_constructor;
void* original_constructor;
void* original_new_a[2];
void* original_new_v[2];
std::atomic<int> rejected{0};

void Deopt(void* thread, void* frame, void* result, bool from_code, int type) {
  trace_deopt(thread, frame, result, from_code, type);
  using Fn = void (*)(void*, void*, void*, bool, int);
  reinterpret_cast<Fn>(original_deopt)(thread, frame, result, from_code, type);
}
size_t Nterp(void* thread, void* caller, uint16_t* dex_pc) {
  if (GuardNterpInvoke(thread, caller, dex_pc)) { ++rejected; return 0; }
  using Fn = size_t (*)(void*, void*, uint16_t*);
  return reinterpret_cast<Fn>(original_nterp)(thread, caller, dex_pc);
}
void* Resolve(void* linker, void* thread, uint32_t index, void* caller, int type) {
  using Fn = void* (*)(void*, void*, uint32_t, void*, int);
  return reinterpret_cast<Fn>(NeedsDispatchChecks() ? checked_resolve : original_resolve)(
      linker, thread, index, caller, type);
}
void* ResolveHandles(void* linker, uint32_t index, void* dex_cache, void* loader, void* caller, int type) {
  using Fn = void* (*)(void*, uint32_t, void*, void*, void*, int);
  return reinterpret_cast<Fn>(NeedsDispatchChecks() ? checked_resolve_handles : original_resolve_handles)(
      linker, index, dex_cache, loader, caller, type);
}
void Invoke(void* method, void* thread, uint32_t* args, uint32_t size, void* result, const char* shorty) {
  if (GuardRetiredMethod(method, thread, result)) { ++rejected; return; }
  using Fn = void (*)(void*, void*, uint32_t*, uint32_t, void*, const char*);
  reinterpret_cast<Fn>(original_invoke)(method, thread, args, size, result, shorty);
}
// ART JValue is an eight-byte union returned in x0 on this pinned arm64 ABI.
uint64_t VirtualJValues(const void* access, jobject receiver, void* method, const jvalue* args) {
  if (GuardRetiredMethod(method, nullptr, nullptr)) { ++rejected; return 0; }
  if (guard_receiver_method(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, void*, const jvalue*);
  return reinterpret_cast<Fn>(original_jvalues)(access, receiver, method, args);
}
uint64_t VirtualVarArgs(const void* access, jobject receiver, void* method, va_list args) {
  if (GuardRetiredMethod(method, nullptr, nullptr)) { ++rejected; return 0; }
  if (guard_receiver_method(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, void*, va_list);
  return reinterpret_cast<Fn>(original_varargs)(access, receiver, method, args);
}
uint64_t IdJValues(const void* access, jobject receiver, jmethodID method, const jvalue* args) {
  if (guard_id(method)) { ++rejected; return 0; }
  if (guard_receiver_id(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, jmethodID, const jvalue*);
  return reinterpret_cast<Fn>(original_id_jvalues)(access, receiver, method, args);
}
uint64_t IdVarArgs(const void* access, jobject receiver, jmethodID method, va_list args) {
  if (guard_id(method)) { ++rejected; return 0; }
  if (guard_receiver_id(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, jmethodID, va_list);
  return reinterpret_cast<Fn>(original_id_varargs)(access, receiver, method, args);
}
uint64_t DirectJValues(const void* access, jobject receiver, void* method, const jvalue* args) {
  if (GuardRetiredMethod(method, nullptr, nullptr)) { ++rejected; return 0; }
  if (receiver != nullptr && guard_receiver_method(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, void*, const jvalue*);
  return reinterpret_cast<Fn>(original_direct_jvalues)(access, receiver, method, args);
}
uint64_t DirectVarArgs(const void* access, jobject receiver, void* method, va_list args) {
  if (GuardRetiredMethod(method, nullptr, nullptr)) { ++rejected; return 0; }
  if (receiver != nullptr && guard_receiver_method(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, void*, va_list);
  return reinterpret_cast<Fn>(original_direct_varargs)(access, receiver, method, args);
}
uint64_t DirectIdJValues(const void* access, jobject receiver, jmethodID method, const jvalue* args) {
  if (guard_id(method)) { ++rejected; return 0; }
  if (receiver != nullptr && guard_receiver_id(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, jmethodID, const jvalue*);
  return reinterpret_cast<Fn>(original_direct_id_jvalues)(access, receiver, method, args);
}
uint64_t DirectIdVarArgs(const void* access, jobject receiver, jmethodID method, va_list args) {
  if (guard_id(method)) { ++rejected; return 0; }
  if (receiver != nullptr && guard_receiver_id(access, receiver, method)) { ++rejected; return 0; }
  using Fn = uint64_t (*)(const void*, jobject, jmethodID, va_list);
  return reinterpret_cast<Fn>(original_direct_id_varargs)(access, receiver, method, args);
}
jobject Reflect(const void* access, jobject method, jobject receiver, jobject args, size_t frames) {
  if (GuardRunnableExecutable(access, method)) { ++rejected; return nullptr; }
  using Fn = jobject (*)(const void*, jobject, jobject, jobject, size_t);
  return reinterpret_cast<Fn>(original_reflection)(access, method, receiver, args, frames);
}
using InvokeHandle = bool (*)(void*, void*, void*, void*, const void*, void*);
bool Handle(void* thread, void* frame, void* handle, void* type, const void* args, void* result) {
  if (guard_handle(thread, handle, result)) { ++rejected; return false; }
  return reinterpret_cast<InvokeHandle>(original_handle)(thread, frame, handle, type, args, result);
}
bool Exact(void* thread, void* frame, void* handle, void* type, const void* args, void* result) {
  if (guard_handle(thread, handle, result)) { ++rejected; return false; }
  return reinterpret_cast<InvokeHandle>(original_exact)(thread, frame, handle, type, args, result);
}
void WithFrame(void* thread, void* handle, void* frame) {
  if (guard_handle(thread, handle, nullptr)) { ++rejected; return; }
  using Fn = void (*)(void*, void*, void*);
  reinterpret_cast<Fn>(original_frame)(thread, handle, frame);
}
uint64_t Obsolete(void* method, void* thread) {
  if (GuardRetiredMethod(method, thread, nullptr)) { ++rejected; DeliverRetiredMemberException(thread); }
  using Fn = uint64_t (*)(void*, void*);
  return reinterpret_cast<Fn>(original_obsolete)(method, thread);
}
bool CheckJniMethod(void* check, void* access, jobject receiver, jclass cls,
                    jmethodID method, int primitive, int invoke) {
  // CheckJNI normally aborts on an obsolete declaring class. Convert only known
  // removed members into an ordinary pending exception; preserve all other checks.
  if (guard_id(method)) { ++rejected; return false; }
  if (receiver != nullptr && guard_receiver_id(access, receiver, method)) { ++rejected; return false; }
  using Fn = bool (*)(void*, void*, jobject, jclass, jmethodID, int, int);
  return reinterpret_cast<Fn>(original_check_jni)(check, access, receiver, cls, method, primitive, invoke);
}
bool CheckConstructor(void* check, jmethodID method) {
  if (guard_id(method)) { ++rejected; return false; }
  using Fn = bool (*)(void*, jmethodID);
  return reinterpret_cast<Fn>(original_check_constructor)(check, method);
}
jobject Constructor(JNIEnv* env, jobject constructor, jobjectArray args) {
  if (guard_jni_executable(env, constructor)) { ++rejected; return nullptr; }
  using Fn = jobject (*)(JNIEnv*, jobject, jobjectArray);
  return reinterpret_cast<Fn>(original_constructor)(env, constructor, args);
}
template <int indexed> jobject NewA(JNIEnv* env, jclass cls, jmethodID method, const jvalue* args) {
  if (guard_jni_id(env, method)) { ++rejected; return nullptr; }
  using Fn = jobject (*)(JNIEnv*, jclass, jmethodID, const jvalue*);
  return reinterpret_cast<Fn>(original_new_a[indexed])(env, cls, method, args);
}
template <int indexed> jobject NewV(JNIEnv* env, jclass cls, jmethodID method, va_list args) {
  if (guard_jni_id(env, method)) { ++rejected; return nullptr; }
  using Fn = jobject (*)(JNIEnv*, jclass, jmethodID, va_list);
  return reinterpret_cast<Fn>(original_new_v[indexed])(env, cls, method, args);
}
}

bool InstallRetiredMethodHooks(HookEngine& hooks, void* library, std::string* error) {
  guard_handle = reinterpret_cast<GuardHandle>(dlsym(library, "ArtDcevmGuardMethodHandle"));
  guard_id = reinterpret_cast<bool (*)(jmethodID)>(dlsym(library, "ArtDcevmGuardMethodId"));
  guard_jni_id = reinterpret_cast<decltype(guard_jni_id)>(dlsym(library, "ArtDcevmGuardJniMethodId"));
  guard_receiver_method = reinterpret_cast<decltype(guard_receiver_method)>(dlsym(library, "ArtDcevmGuardRunnableMethodReceiver"));
  guard_receiver_id = reinterpret_cast<decltype(guard_receiver_id)>(dlsym(library, "ArtDcevmGuardRunnableMethodIdReceiver"));
  guard_jni_executable = reinterpret_cast<decltype(guard_jni_executable)>(dlsym(library, "ArtDcevmGuardJniExecutable"));
  if (!guard_handle || !guard_id ||
      !guard_jni_id || !guard_jni_executable || !guard_receiver_method || !guard_receiver_id) {
    *error = "Retired method guard exports are incomplete"; return false;
  }
  trace_deopt = reinterpret_cast<decltype(trace_deopt)>(dlsym(library, "ArtDcevmSourceTraceDeopt"));
  auto configure_dispatch = reinterpret_cast<void* (*)(void (*)())>(
      dlsym(library, "ArtDcevmSourceConfigureDispatchChecks"));
  checked_resolve = hooks.FindSymbol(symbols::LibraryName(symbols::Library::kArt), cas::kCheckedResolve);
  checked_resolve_handles = hooks.FindSymbol(symbols::LibraryName(symbols::Library::kArt), cas::kCheckedResolveHandles);
  if (!trace_deopt || !configure_dispatch || !checked_resolve || !checked_resolve_handles) {
    *error = "Dispatch validation symbols are incomplete"; return false;
  }
  if (!ConfigureNterpDispatch(configure_dispatch(RequireDispatchChecks),
      reinterpret_cast<CheckedDispatchResolver>(checked_resolve))) {
    *error = "Named checked resolver or source ClassLinker accessor is unavailable";
    return false;
  }
  struct Entry { const char* symbol; void* proxy; void** original; void* target = nullptr; };
  Entry entries[] = {
    {cas::kRetiredMethodHooks[0], reinterpret_cast<void*>(Deopt), &original_deopt},
    {cas::kRetiredMethodHooks[1], reinterpret_cast<void*>(Nterp), &original_nterp},
    {cas::kRetiredMethodHooks[2], reinterpret_cast<void*>(Resolve), &original_resolve},
    {cas::kRetiredMethodHooks[3], reinterpret_cast<void*>(ResolveHandles), &original_resolve_handles},
    {cas::kRetiredMethodHooks[4], reinterpret_cast<void*>(Invoke), &original_invoke},
    {cas::kRetiredMethodHooks[5], reinterpret_cast<void*>(VirtualJValues), &original_jvalues},
    {cas::kRetiredMethodHooks[6], reinterpret_cast<void*>(VirtualVarArgs), &original_varargs},
    {cas::kRetiredMethodHooks[7], reinterpret_cast<void*>(IdJValues), &original_id_jvalues},
    {cas::kRetiredMethodHooks[8], reinterpret_cast<void*>(IdVarArgs), &original_id_varargs},
    {cas::kRetiredMethodHooks[9], reinterpret_cast<void*>(DirectJValues), &original_direct_jvalues},
    {cas::kRetiredMethodHooks[10], reinterpret_cast<void*>(DirectVarArgs), &original_direct_varargs},
    {cas::kRetiredMethodHooks[11], reinterpret_cast<void*>(DirectIdJValues), &original_direct_id_jvalues},
    {cas::kRetiredMethodHooks[12], reinterpret_cast<void*>(DirectIdVarArgs), &original_direct_id_varargs},
    {cas::kRetiredMethodHooks[13], reinterpret_cast<void*>(Reflect), &original_reflection},
    {cas::kRetiredMethodHooks[14], reinterpret_cast<void*>(Handle), &original_handle},
    {cas::kRetiredMethodHooks[15], reinterpret_cast<void*>(Exact), &original_exact},
    {cas::kRetiredMethodHooks[16], reinterpret_cast<void*>(WithFrame), &original_frame},
    {cas::kRetiredMethodHooks[17], reinterpret_cast<void*>(Obsolete), &original_obsolete},
    {cas::kRetiredMethodHooks[18], reinterpret_cast<void*>(CheckJniMethod), &original_check_jni},
    {cas::kRetiredMethodHooks[19], reinterpret_cast<void*>(CheckConstructor), &original_check_constructor},
    {cas::kRetiredMethodHooks[20], reinterpret_cast<void*>(Constructor), &original_constructor},
    {cas::kRetiredMethodHooks[21], reinterpret_cast<void*>(NewA<0>), &original_new_a[0]},
    {cas::kRetiredMethodHooks[22], reinterpret_cast<void*>(NewA<1>), &original_new_a[1]},
    {cas::kRetiredMethodHooks[23], reinterpret_cast<void*>(NewV<0>), &original_new_v[0]},
    {cas::kRetiredMethodHooks[24], reinterpret_cast<void*>(NewV<1>), &original_new_v[1]},
  };
  for (auto& entry : entries) {
    entry.target = hooks.FindSymbol(symbols::LibraryName(symbols::Library::kArt), entry.symbol);
    if (!entry.target) { *error = std::string("Missing invocation symbol: ") + entry.symbol; return false; }
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
int RejectedRetiredInvocations() { return rejected.load(); }
}

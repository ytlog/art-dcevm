#include "platform/common/jni_interface_dispatch.h"
#include <atomic>
#include <cstdarg>
#include <mutex>
#include "evolution/modifiers.h"
#include "platform/abi/arm64/common/jni_invoke.h"
#include "platform/common/art_hook.h"
#include "platform/common/member_mapping.h"
#include "platform/symbols/jni_interface_dispatch_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::jni_interface_dispatch {
namespace {
using Value = abi::arm64::JniInvokeValue;
using VarArgs = Value (*)(const void*, jobject, jmethodID, va_list);
using JValues = Value (*)(const void*, jobject, jmethodID, const jvalue*);
VarArgs original_varargs = nullptr;
JValues original_jvalues = nullptr;
VarArgs original_direct_varargs = nullptr;
JValues original_direct_jvalues = nullptr;
void* installed_hooks[4]{};
JavaVM* machine = nullptr;
jvmtiEnv* environment = nullptr;
void* (*current_thread)() = nullptr;
void (*throw_exception)(void*, const char*, const char*) = nullptr;
std::mutex install_mutex;
std::atomic<bool> configured{false}, ready{false}, enabled{false};

bool Reject(jobject receiver, jmethodID method) {
  if (!enabled.load(std::memory_order_acquire) || !receiver || !method) return false;
  JNIEnv* jni = nullptr;
  if (machine->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni) {
    throw_exception(current_thread(), "Ljava/lang/InternalError;", "JNI receiver environment unavailable");
    return true;
  }
  if (jni->ExceptionCheck()) return false;
  // Public JVMTI decodes both pointer and index IDs. Do not retain app Classes,
  // method classifications or a caller's disposable JVMTI environment.
  jclass declaring = nullptr;
  jint modifiers = 0;
  bool readable = environment->GetMethodDeclaringClass(method, &declaring) == JVMTI_ERROR_NONE && declaring &&
      environment->GetClassModifiers(declaring, &modifiers) == JVMTI_ERROR_NONE;
  bool incompatible = readable && (modifiers & evolution::kAccInterface) && !jni->IsInstanceOf(receiver, declaring);
  if (declaring) jni->DeleteLocalRef(declaring);
  if (jni->ExceptionCheck()) return true;
  if (!readable || incompatible) {
    throw_exception(current_thread(), readable ? "Ljava/lang/IncompatibleClassChangeError;" : "Ljava/lang/InternalError;",
                    readable ? "JNI receiver lost the declaring interface" : "JNI declaring class unavailable");
    return true;
  }
  return false;
}
Value InvokeVarArgs(const void* soa, jobject receiver, jmethodID method, va_list args) {
  return Reject(receiver, method) ? Value{} : original_varargs(soa, receiver, method, args);
}
Value InvokeJValues(const void* soa, jobject receiver, jmethodID method, const jvalue* args) {
  return Reject(receiver, method) ? Value{} : original_jvalues(soa, receiver, method, args);
}
Value InvokeDirectVarArgs(const void* soa, jobject receiver, jmethodID method, va_list args) {
  return original_direct_varargs(soa, receiver,
      enabled.load(std::memory_order_acquire) ? member_mapping::CopiedJniTarget(method) : method, args);
}
Value InvokeDirectJValues(const void* soa, jobject receiver, jmethodID method, const jvalue* args) {
  return original_direct_jvalues(soa, receiver,
      enabled.load(std::memory_order_acquire) ? member_mapping::CopiedJniTarget(method) : method, args);
}
}  // namespace

void Configure(JavaVM* vm, jvmtiEnv* env) {
  machine = vm; environment = env;
  configured.store(vm && env, std::memory_order_release);
}
bool Prepare() {
  if (ready.load(std::memory_order_acquire)) return true;
  if (!configured.load(std::memory_order_acquire)) return false;
  std::lock_guard<std::mutex> lock(install_mutex);
  if (ready.load(std::memory_order_relaxed)) return true;
  using namespace symbols::jni_interface_dispatch;
  struct Entry { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Entry entries[] = {
      {kVirtualVarArgs, HookAddress(InvokeVarArgs), HookSlot(&original_varargs)},
      {kVirtualJValues, HookAddress(InvokeJValues), HookSlot(&original_jvalues)},
      {kDirectVarArgs, HookAddress(InvokeDirectVarArgs), HookSlot(&original_direct_varargs)},
      {kDirectJValues, HookAddress(InvokeDirectJValues), HookSlot(&original_direct_jvalues)}};
  void* targets[4]{};
  for (size_t i = 0; i < 4; ++i) if (!(targets[i] = symbols::Find(entries[i].symbol))) return false;
  current_thread = reinterpret_cast<decltype(current_thread)>(symbols::Find(symbols::runtime_thread::kCurrentThread));
  throw_exception = reinterpret_cast<decltype(throw_exception)>(symbols::Find(symbols::structural::kThrowNewException));
  if (!current_thread || !throw_exception) return false;
  for (size_t i = 0; i < 4; ++i) {
    if (!installed_hooks[i]) installed_hooks[i] = shadowhook_hook_sym_addr(
        targets[i], entries[i].replacement, entries[i].original);
    if (!installed_hooks[i]) {
      // Failed unhooks retain forwarding hooks that the next retry reuses.
      for (size_t j = i; j-- > 0;)
        if (installed_hooks[j] && shadowhook_unhook(installed_hooks[j]) == 0) installed_hooks[j] = nullptr;
      return false;
    }
  }
  ready.store(true, std::memory_order_release);
  return true;
}
bool Enable() {
  if (!ready.load(std::memory_order_acquire)) return false;
  enabled.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::jni_interface_dispatch
